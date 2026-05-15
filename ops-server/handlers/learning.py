"""
handlers/learning.py  —  학습 / 추론 핸들러 (NO.201 ~ 206)

담당 메시지:
  NO.201 REQ_DAILY_WORDS  → NO.202 RES_DAILY_WORDS   오늘의 단어
  NO.203 REQ_INFER        → NO.204 RES_INFER          수화 추론
  NO.205 REQ_REVIEW_WORDS → NO.206 RES_REVIEW_WORDS   복습 단어

추론 흐름:
  클라이언트 frames 수신
  → NO.501 REQ_AI_INFER 전송 (AI 서버)
  → NO.502 RES_AI_INFER 수신
  → 판정(result/accuracy/is_trained_candidate) 후 클라이언트에 응답
  → 비동기로 DB 저장 (user_progress, user_history, keypoint_store)
"""

import asyncio
import json
import time
import uuid
import os

from datetime import datetime
from typing import Optional

from logger import get_logger
from protocol import Protocol, MessageType, make_error, ErrorCode
from handlers.base import BaseHandler, KEYPOINT_VER, MIN_FRAMES, MAX_FRAMES
VIDEO_SERVER = os.getenv("VIDEO_SERVER", "http://10.10.10.114:8000")

logger = get_logger("handler.learning")


class LearningHandler(BaseHandler):

    def __init__(self, db_manager, session_manager, ai_pool, config,
                 track_task_fn, save_retry_queue: asyncio.Queue):
        super().__init__(db_manager, session_manager, ai_pool, config)
        # OpsServer._track_task: 비동기 태스크를 서버 생명주기에 등록하는 콜백
        self._track_task      = track_task_fn
        # DB 저장 실패 시 재시도 큐 (OpsServer 와 공유)
        self._save_retry_queue = save_retry_queue
        # 추론 통계 카운터
        self.stat_ok  = 0
        self.stat_err = 0

    # ── NO.201 REQ_DAILY_WORDS → NO.202 RES_DAILY_WORDS ──────

    async def handle_daily_words(self, msg: dict, session_id: str) -> dict:
        """
        아직 학습하지 않은 단어를 difficulty ASC 순으로 최대 10개 반환.
        DB get_word_list(mode='study') 로 처리한다.
        """
        user_id = self._get_user_id(session_id)
        if not user_id:
            return self._not_authenticated()

        logger.debug(f"[DailyWords] user={user_id} 오늘의 단어 요청")
        try:
            words = await self.db.get_word_list(user_id, mode="study")
            # ↓ 추가
            today_completed_count = await self.db.get_today_completed_count(user_id)
            today_learned_words = await self.db.get_today_learned_words(user_id)
            # ↑

            logger.debug(f"[DailyWords] user={user_id} → {len(words)}개 반환")
            return {
                "type"  : MessageType.RES_DAILY_WORDS,
                "status": "ok",
                "words" : words,  # NO.202: [{word_id, word, video_cdn_url}]
                "today_completed_count": today_completed_count,  # ← 추가
                "today_learned_words": today_learned_words,  # ← 추가
            }
        except Exception as e:
            logger.error(f"[DailyWords] user={user_id} 조회 오류: {e}", exc_info=True)
            return make_error(ErrorCode.SERVER_ERROR, "server_error")

    # ── NO.203 REQ_INFER → NO.204 RES_INFER ──────────────────

    async def handle_infer(self, msg: dict, session_id: str) -> dict:
        """
        클라이언트가 보낸 frames 를 AI 서버에 넘겨 추론하고
        정답/오답 판정 결과를 반환한다.

        NO.203 요청 필드:
          word_id, keypoint_version, total_frames,
          frames: [{frame_idx, left_hand, right_hand, pose}]

        NO.204 응답 필드:
          result(bool)  — true=정답 / false=오답
          accuracy(float 0.0~1.0)
          is_trained_candidate(bool) — 재학습 후보 여부
        """
        start_ts = time.monotonic()
        sid_short = session_id[:8]

        user_id = self._get_user_id(session_id)
        if not user_id:
            logger.warning(f"[Infer] sid={sid_short} 인증 없이 추론 요청 — 거절")
            return self._not_authenticated()

        target_word_id   = msg.get("word_id")
        keypoint_version = msg.get("keypoint_version", KEYPOINT_VER)
        total_frames     = msg.get("total_frames", 0)
        frames           = msg.get("frames", [])

        logger.debug(
            f"[Infer] user={user_id} word_id={target_word_id} "
            f"frames={len(frames)}개 수신 "
            f"첫 프레임 키 샘플={list(frames[0].keys()) if frames else '없음'}"
        )

        logger.debug(
            f"[Infer] user={user_id} word_id={target_word_id} "
            f"total_frames={total_frames} kp_ver={keypoint_version}"
        )

        # frames 유효성 검사
        err = self._validate_frames(frames)
        if err:
            logger.warning(f"[Infer] user={user_id} frames 유효성 오류: {err}")
            return err

        # ↓ 여기에 추가
        import os
        log_dir = "keypoint_logs"
        os.makedirs(log_dir, exist_ok=True)
        log_path = os.path.join(
            log_dir,
            f"user{user_id}_word{target_word_id}_{datetime.utcnow().strftime('%Y%m%d_%H%M%S')}.json"
        )
        with open(log_path, "w", encoding="utf-8") as f:
            json.dump({
                "user_id": user_id,
                "word_id": target_word_id,
                "frames": frames,
            }, f, ensure_ascii=False, indent=2)
        # ↑ 여기까지

        # AI 추론 (NO.501 → NO.502)
        request_id       = str(uuid.uuid4())
        model_version_id = await self.db.get_active_model_version_id()

        try:
            ai_result = await asyncio.wait_for(
                self._request_ai_inference(
                    request_id       = request_id,
                    model_version_id = model_version_id,
                    keypoint_version = keypoint_version,
                    total_frames     = total_frames,
                    frames           = frames,
                ),
                timeout=self.config.AI_TIMEOUT,
            )
        except asyncio.TimeoutError:
            self.stat_err += 1
            logger.error(f"[Infer] user={user_id} AI 타임아웃 ({self.config.AI_TIMEOUT}s)")
            return make_error(ErrorCode.AI_TIMEOUT, "AI 추론 시간 초과")
        except RuntimeError:
            self.stat_err += 1
            return make_error(ErrorCode.AI_POOL_EXHAUSTED, "ai_pool_exhausted")
        except Exception as e:
            self.stat_err += 1
            logger.error(f"[Infer] user={user_id} AI 통신 예외: {e}", exc_info=True)
            return make_error(ErrorCode.AI_INFER_ERROR, "ai_error")

        # NO.502 필드 파싱
        predicted_word_id = ai_result.get("predicted_word_id")
        confidence        = ai_result.get("confidence", 0.0)
        inference_ms      = ai_result.get("inference_ms", 0)

        # confidence 범위 검증
        if not isinstance(confidence, (int, float)) or not (0.0 <= confidence <= 1.0):
            self.stat_err += 1
            logger.error(f"[Infer] user={user_id} 비정상 confidence={confidence!r}")
            return make_error(ErrorCode.INVALID_AI_RESPONSE, "invalid_ai_response")

        # # 판정
        # # - is_correct: 예측 단어 일치 AND confidence ≥ CONFIDENCE_CORRECT
        # # - is_trained_candidate: confidence ≥ CONFIDENCE_CANDIDATE → keypoint 저장 대상
        # is_correct           = (predicted_word_id == target_word_id) and \
        #                        (confidence >= self.config.CONFIDENCE_CORRECT)
        # # 수정 후 — 문서 기준: word_id 일치 AND confidence >= 0.5
        # is_trained_candidate = (predicted_word_id == target_word_id) and \
        #                        (confidence >= self.config.CONFIDENCE_CANDIDATE)

        # 설정값 (예시: config에 정의된 값 사용)
        # self.config.CONFIDENCE_CORRECT = 0.8
        # self.config.CONFIDENCE_CANDIDATE = 0.5

        # 1. 단어 일치 여부 확인
        is_word_matched = (predicted_word_id == target_word_id)

        # [판정 기준 적용]
        # 1. 단어가 맞지 않으면 오답 (is_correct = False)
        # 2. 단어가 같으면서 0.8 이상은 정답 (is_correct = True)
        is_correct = is_word_matched and (confidence >= self.config.CONFIDENCE_CORRECT)

        # [저장 기준 적용 (재학습 후보군)]
        # 3. 단어가 같으면서 0.5 <= confidence < 0.8 구간은 오답이지만 DB에 저장 (is_trained_candidate = True)
        # 4. 단어가 같더라도 0.5 미만이면 저장하지 않음 (is_trained_candidate = False)
        is_trained_candidate = is_word_matched and (confidence >= self.config.CONFIDENCE_CANDIDATE)

        # 결과 요약 (참고용)
        # - 정답: is_correct=True (자동으로 candidate에도 포함됨)
        # - 오답이지만 저장: is_correct=False AND is_trained_candidate=True
        # - 오답이며 미저장: is_correct=False AND is_trained_candidate=False

        self.stat_ok += 1

        elapsed_total = int((time.monotonic() - start_ts) * 1000)
        target_name = self.db._word_cache.get(target_word_id, {}).get("word", str(target_word_id))
        target_number = self.db._word_cache.get(target_word_id, {}).get("word_number", "?")
        predicted_name = self.db._word_cache.get(predicted_word_id, {}).get("word", str(predicted_word_id))
        predicted_number = self.db._word_cache.get(predicted_word_id, {}).get("word_number", "?")

        logger.info(
            f"[Infer] user={user_id} "
            f"정답단어={target_name}({target_number}) "
            f"예측단어={predicted_name}({predicted_number}) "
            f"result={'정답' if is_correct else '오답'} "
            f"accuracy={confidence:.4f} inference_ms={inference_ms} total={elapsed_total}ms"
        )

        # 정답인 경우 user_progress 즉시 저장 (홈 화면 갱신을 위해)
        if is_correct:
            try:
                async with self.db.acquire() as conn:
                    async with conn.cursor() as cur:
                        await conn.begin()
                        await cur.execute(
                            """
                            INSERT INTO user_progress
                                (user_id, word_id, accuracy, attempts, last_practiced)
                            VALUES (%s, %s, %s, 1, NOW())
                            ON DUPLICATE KEY UPDATE
                                accuracy       = (accuracy * attempts + VALUES(accuracy)) / (attempts + 1),
                                attempts       = attempts + 1,
                                last_practiced = NOW()
                            """,
                            (user_id, target_word_id, confidence),
                        )
                        await conn.commit()
            except Exception as e:
                logger.warning(f"[Infer] user_progress 즉시 저장 실패: {e}")

        # user_history, keypoint_store는 기존대로 비동기 처리
        self._track_task(
            self._save_result_with_retry(
                user_id=user_id,
                word_id=target_word_id,
                is_correct=is_correct,
                confidence=confidence,
                frames=frames if is_trained_candidate else None,
                session_id=session_id,
            )
        )

        return {
            "type": MessageType.RES_INFER,
            "status": "ok",
            "word_id": target_word_id,
            "result": is_correct,
            "accuracy": round(confidence, 4),
        }

        # # DB 저장은 응답 후 비동기로 처리 (응답 지연 방지)
        # self._track_task(
        #     self._save_result_with_retry(
        #         user_id   = user_id,
        #         word_id   = target_word_id,
        #         is_correct= is_correct,
        #         confidence= confidence,
        #         frames    = frames if is_trained_candidate else None,  # 후보만 저장
        #         session_id= session_id,
        #     )
        # )
        #
        # return {
        #     "type"                : MessageType.RES_INFER,
        #     "status"              : "ok",
        #     "word_id"             : target_word_id,
        #     "result"              : is_correct,               # NO.204: true/false
        #     "accuracy"            : round(confidence, 4),     # NO.204: 0.0~1.0
        # }

    # ── NO.205 REQ_REVIEW_WORDS → NO.206 RES_REVIEW_WORDS ────

    # ── NO.205 REQ_REVIEW_WORDS → NO.206 RES_REVIEW_WORDS ────

    async def handle_review_words(self, msg: dict, session_id: str) -> dict:
        """
        accuracy < 0.8 이거나 attempts < 3 인 단어를 최대 20개 반환.
        DB get_word_list(mode='review') 로 처리한다.
        """
        user_id = self._get_user_id(session_id)
        if not user_id:
            return self._not_authenticated()

        logger.debug(f"[ReviewWords] user={user_id} 복습 단어 요청")
        try:
            words = await self.db.get_word_list(user_id, mode="review")


            logger.debug(f"[ReviewWords] user={user_id} → {len(words)}개 반환")
            return {
                "type": MessageType.RES_REVIEW_WORDS,
                "status": "ok",
                "words": words,  # NO.206: [{word_id, word, video_cdn_url, ...}]
            }
        except Exception as e:
            logger.error(f"[ReviewWords] user={user_id} 조회 오류: {e}", exc_info=True)
            return make_error(ErrorCode.SERVER_ERROR, "server_error")

    # ── AI 추론 공통 메서드 (NO.501 → NO.502) ─────────────────

    async def _request_ai_inference(
        self,
        request_id      : str,
        model_version_id: int,
        keypoint_version: str,
        total_frames    : int,
        frames          : list,
    ) -> dict:
        """
        AI 연결 풀에서 연결을 꺼내 REQ_AI_INFER(NO.501)을 전송하고
        RES_AI_INFER(NO.502)을 수신해 반환한다.
        learning, dictionary, game 핸들러가 공통으로 호출한다.
        """
        t0 = time.monotonic()
        async with self.ai_pool.acquire(timeout=2.0) as (reader, writer):
            if reader is None or writer is None:
                raise ConnectionError("AI 서버 연결 없음")

            payload = {
                "type"            : MessageType.REQ_AI_INFER,
                "request_id"      : request_id,        # NO.501: 요청-응답 매칭용 UUID
                "model_version_id": model_version_id,  # NO.501: 사용할 모델 버전
                "keypoint_version": keypoint_version,  # NO.501: keypoint 포맷 버전
                "total_frames"    : total_frames,      # NO.501: 전체 프레임 수
                "frames"          : frames,            # NO.501: keypoint 시퀀스
            }
            await Protocol.send_message(writer, payload)
            logger.debug(
                f"[AI] REQ_AI_INFER 전송: request_id={request_id[:8]} "
                f"model_version_id={model_version_id} frames={total_frames}"
            )

            result = await Protocol.recv_message(reader)
            elapsed_ms = int((time.monotonic() - t0) * 1000)

            # 응답 타입 / request_id 일치 여부 경고 (치명적이지 않음)
            if result and result.get("type") != MessageType.RES_AI_INFER:
                logger.warning(f"[AI] 예상치 못한 응답 type: {result.get('type')}")
            if result and result.get("request_id") != request_id:
                logger.warning(
                    f"[AI] request_id 불일치: "
                    f"요청={request_id[:           8]} 응답={str(result.get('request_id',''))[:8]}"
                )

            logger.debug(
                f"[AI] RES_AI_INFER 수신 ({elapsed_ms}ms): "
                f"predicted_word_id={result.get('predicted_word_id') if result else None} "
                f"confidence={result.get('confidence') if result else None}"
            )
            if result is None:
                raise ConnectionResetError("AI 서버 연결 끊김 — 응답 없음")
            return result

    # ── DB 저장 (비동기 재시도 포함) ──────────────────────────

    async def _save_result_with_retry(self, **kwargs):
        """
        DB 저장 실패 시 재시도 큐에 넣는다.
        큐가 가득 차면 데이터를 버리고 ERROR 로그를 남긴다.
        """
        try:
            await self._save_result(**kwargs)
            logger.debug(
                f"[DB] 저장 완료: user={kwargs.get('user_id')} word={kwargs.get('word_id')}"
            )
        except Exception as e:
            logger.warning(
                f"[DB] 저장 실패({type(e).__name__}) → 재시도 큐 등록 "
                f"user={kwargs.get('user_id')} word={kwargs.get('word_id')} "
                f"큐={self._save_retry_queue.qsize()}"
            )
            try:
                self._save_retry_queue.put_nowait(kwargs)
            except asyncio.QueueFull:
                logger.error(
                    f"[DB] 재시도 큐 포화(max={self._save_retry_queue.maxsize}) "
                    f"— user={kwargs.get('user_id')} 데이터 버림"
                )

    async def _save_result(
        self,
        user_id   : int,
        word_id   : int,
        is_correct: bool,
        confidence: float,
        frames,          # list(재학습 후보) 또는 None(저장 불필요)
        session_id: str,
    ):
        """
        추론 결과를 세 테이블에 저장한다.
          1. user_progress  — 정확도/시도횟수 누적 (UPSERT)
          2. user_history   — 추론 이력 JSON INSERT
          3. keypoint_store — keypoint_consent=True 이고 재학습 후보인 경우만 INSERT
        """

        t0 = time.monotonic()
        try:
            async with self.db.acquire(timeout=5.0) as conn:
                async with conn.cursor() as cur:
                    await conn.begin()  # ← 여기에 추가
                    # 1. user_progress UPSERT: 이동 평균으로 accuracy 갱신
                    await cur.execute(
                        """
                        INSERT INTO user_progress
                            (user_id, word_id, accuracy, attempts, last_practiced)
                        VALUES (%s, %s, %s, 1, NOW())
                        ON DUPLICATE KEY UPDATE
                            accuracy       = (accuracy * attempts + VALUES(accuracy)) / (attempts + 1),
                            attempts       = attempts + 1,
                            last_practiced = NOW()
                        """,
                        (user_id, word_id, confidence),
                    )

                    # 2. user_history INSERT
                    history_entry = {
                        "word_id"  : word_id,
                        "result"   : is_correct,
                        "accuracy" : round(confidence, 4),
                        "timestamp": datetime.utcnow().isoformat(),
                    }
                    await cur.execute(
                        "INSERT INTO user_history "
                        "(user_id, session_id, history_json) VALUES (%s, %s, %s)",
                        (user_id, session_id, json.dumps(history_entry)),
                    )

                    # 3. keypoint_store INSERT (동의 + 재학습 후보인 경우만)
                    if frames is not None:
                        consent = await self.db.get_user_consent(user_id, conn)
                        if consent:
                            # 딕셔너리 frames → (T, 134) 플랫 배열 변환
                            # pose 25×2 + left_hand 21×2 + right_hand 21×2 = 134차원
                            # c(confidence) 제외, is_gongsu 프레임 제외
                            flat_frames = []
                            for frame in frames:
                                if frame.get("is_gongsu", False):
                                    continue
                                pose = [v for joint in frame.get("pose", []) for v in joint[:2]]
                                left_hand = [v for joint in frame.get("left_hand", []) for v in joint[:2]]
                                right_hand = [v for joint in frame.get("right_hand", []) for v in joint[:2]]
                                flat_frames.append(pose + left_hand + right_hand)

                            if flat_frames:
                                await cur.execute(
                                    "INSERT INTO keypoint_store "
                                    "(user_id, word_id, keypoint_data, confidence, is_trained) "
                                    "VALUES (%s, %s, %s, %s, 0)",
                                    (user_id, word_id, json.dumps(flat_frames), confidence),
                                )
                            else:
                                logger.debug(f"[DB] keypoint_store 생략 — 유효 프레임 없음 (is_gongsu 제외 후 0개)")
                        else:
                            logger.debug(f"[DB] keypoint_store 생략 — user={user_id} 미동의")
                    else:
                        logger.debug(f"[DB] keypoint_store 생략 — confidence 하한 미달")

                    await conn.commit()

            # word_stats 는 별도 트랜잭션으로 비동기 처리
            self._track_task(self.db.update_word_stats(word_id, confidence))

            elapsed_ms = int((time.monotonic() - t0) * 1000)
            logger.debug(
                f"[DB] _save_result 완료: user={user_id} word={word_id} ({elapsed_ms}ms)"
            )
        except Exception as e:
            logger.error(
                f"[DB] _save_result 실패: user={user_id} word={word_id}: "
                f"{type(e).__name__}: {e}",
                exc_info=True
            )
            raise
