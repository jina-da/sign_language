"""
handlers/game.py  —  게임 핸들러 (NO.401 ~ 406)

담당 메시지:
  NO.401 REQ_GAME_START → NO.402 RES_GAME_START   게임 시작
  NO.403 REQ_GAME_INFER → NO.404 RES_GAME_INFER   게임 중 추론
  NO.405 REQ_GAME_END   → NO.406 RES_GAME_END     게임 종료

게임 상태 관리:
  OpsServer._active_games(dict)를 공유 참조한다.
    key  : game_id (UUID 문자열)
    value: {user_id, mode, words, results, started_at}

  REQ_GAME_START → game_id 발급 후 _active_games 에 등록
  REQ_GAME_INFER → game_id 로 상태 조회 후 결과 누적
  REQ_GAME_END   → _active_games.pop() 후 DB 저장 및 최종 결과 반환
"""

import asyncio
import time
import uuid

from logger import get_logger
from protocol import MessageType, make_error, ErrorCode
from handlers.base import BaseHandler, KEYPOINT_VER

logger = get_logger("handler.game")


class GameHandler(BaseHandler):

    def __init__(self, db_manager, session_manager, ai_pool, config,
                 active_games: dict, learning_handler):
        super().__init__(db_manager, session_manager, ai_pool, config)
        # OpsServer._active_games 를 직접 참조 (서버 전역 게임 상태)
        self._active_games = active_games
        # AI 추론 공통 메서드를 LearningHandler 에서 재사용
        self._learning     = learning_handler

    # ── NO.401 REQ_GAME_START → NO.402 RES_GAME_START ────────

    async def handle_game_start(self, msg: dict, session_id: str) -> dict:
        """
        게임 세션을 생성하고 출제 단어 목록을 반환한다.
        game_id(UUID)를 발급해 _active_games 에 상태를 등록한다.

        NO.401 요청 필드: mode("time_attack" | "survival")
        NO.402 응답 필드: game_id, words([{word_id, word}])
        """
        user_id = self._get_user_id(session_id)
        if not user_id:
            return self._not_authenticated()

        mode = msg.get("mode", "time_attack")
        logger.info(f"[GameStart] user={user_id} mode={mode} 게임 시작 요청")

        try:
            # 게임용 단어: 사용자가 이미 학습한 단어 중 랜덤 최대 30개
            words = await self.db.get_word_list(user_id, mode="game")
        except Exception as e:
            logger.error(f"[GameStart] 단어 조회 오류: {e}", exc_info=True)
            return make_error(ErrorCode.SERVER_ERROR, "server_error")

        game_id   = str(uuid.uuid4())
        word_list = [{"word_id": w["id"], "word": w["word"]} for w in words]

        # 게임 상태 등록
        self._active_games[game_id] = {
            "user_id"   : user_id,
            "mode"      : mode,
            "words"     : word_list,
            "results"   : [],             # 추론 결과 누적: [{word_id, result, accuracy, score}]
            "started_at": time.monotonic(),
        }

        logger.info(
            f"[GameStart] user={user_id} game_id={game_id[:8]} "
            f"mode={mode} 단어={len(word_list)}개"
        )
        return {
            "type"   : MessageType.RES_GAME_START,
            "status" : "ok",
            "game_id": game_id,    # NO.402: 이후 INFER / END 요청에 사용
            "words"  : word_list,  # NO.402: 출제 단어 목록
        }

    # ── NO.403 REQ_GAME_INFER → NO.404 RES_GAME_INFER ────────

    async def handle_game_infer(self, msg: dict, session_id: str) -> dict:
        """
        게임 중 단어 한 개에 대한 수화 추론을 수행한다.
        결과(정오/정확도/점수)를 game["results"] 에 누적하고 클라이언트에 반환한다.

        NO.403 요청 필드: game_id, word_id, total_frames, frames
        NO.404 응답 필드: result(bool), accuracy(0.0~1.0), score
        """
        user_id = self._get_user_id(session_id)
        if not user_id:
            return self._not_authenticated()

        game_id        = msg.get("game_id", "")
        target_word_id = msg.get("word_id")
        total_frames   = msg.get("total_frames", 0)
        frames         = msg.get("frames", [])

        # game_id 로 진행 중인 게임 확인
        game = self._active_games.get(game_id)
        if not game:
            logger.warning(f"[GameInfer] user={user_id} game_id={game_id[:8]} 없음 또는 종료됨")
            return make_error(ErrorCode.GAME_NOT_FOUND, "game_not_found")

        # frames 유효성 검사
        err = self._validate_frames(frames)
        if err:
            return err

        # AI 추론 (LearningHandler 공통 메서드 재사용)
        request_id       = str(uuid.uuid4())
        model_version_id = await self.db.get_active_model_version_id()

        try:
            ai_result = await asyncio.wait_for(
                self._learning._request_ai_inference(
                    request_id       = request_id,
                    model_version_id = model_version_id,
                    keypoint_version = KEYPOINT_VER,
                    total_frames     = total_frames,
                    frames           = frames,
                ),
                timeout=self.config.AI_TIMEOUT,
            )
        except asyncio.TimeoutError:
            return make_error(ErrorCode.AI_TIMEOUT, "AI 추론 시간 초과")
        except Exception as e:
            logger.error(f"[GameInfer] AI 오류: {e}", exc_info=True)
            return make_error(ErrorCode.AI_INFER_ERROR, "ai_error")

        predicted_word_id = ai_result.get("predicted_word_id")
        confidence        = ai_result.get("confidence", 0.0)

        # 판정 및 점수 계산  정답 단어 = 추론 단어 비교
        is_correct = (predicted_word_id == target_word_id) and \
                     (confidence >= self.config.CONFIDENCE_CORRECT)

        # ← 여기에 추가
        if predicted_word_id != target_word_id:
            logger.warning(
                f"[Infer] 판정 불일치: user={user_id} "
                f"정답={target_word_id} 예측={predicted_word_id} "
                f"confidence={confidence:.4f}"
            )

        logger.info(
            f"[Infer] user={user_id} word={target_word_id} "
            f"predicted={predicted_word_id} "
            f"result={'정답' if is_correct else '오답'} "
            f"accuracy={confidence:.4f} inference_ms={inference_ms} total={elapsed_total}ms"
        )

        score = int(confidence * 100) if is_correct else 0  # 정답 시 정확도 비례 점수

        # 결과 누적 (REQ_GAME_END 에서 합산)
        game["results"].append({
            "word_id" : target_word_id,
            "result"  : is_correct,
            "accuracy": round(confidence, 4),
            "score"   : score,
        })

        logger.debug(
            f"[GameInfer] user={user_id} game={game_id[:8]} "
            f"word={target_word_id} result={is_correct} score={score}"
        )
        return {
            "type"    : MessageType.RES_GAME_INFER,
            "status"  : "ok",
            "result"  : is_correct,           # NO.404
            "accuracy": round(confidence, 4), # NO.404
            "score"   : score,                # NO.404
        }

    # ── NO.405 REQ_GAME_END → NO.406 RES_GAME_END ────────────

    async def handle_game_end(self, msg: dict, session_id: str) -> dict:
        """
        게임을 종료하고 최종 결과를 반환한다.
        _active_games 에서 상태를 꺼내(pop) DB 에 저장한다.

        NO.405 요청 필드: game_id
        NO.406 응답 필드: score(총합), correct_count, total_count
        """
        user_id = self._get_user_id(session_id)
        if not user_id:
            return self._not_authenticated()

        game_id = msg.get("game_id", "")
        # pop 으로 꺼내면 이후 중복 종료 요청이 GAME_NOT_FOUND 로 처리됨
        game = self._active_games.pop(game_id, None)

        if not game:
            logger.warning(f"[GameEnd] user={user_id} game_id={game_id[:8]} 없음")
            return make_error(ErrorCode.GAME_NOT_FOUND, "game_not_found")

        results       = game["results"]
        total_score   = sum(r["score"] for r in results)           # 전체 점수 합산
        correct_count = sum(1 for r in results if r["result"])     # 정답 수
        total_count   = len(results)                               # 전체 시도 수
        duration_sec  = int(time.monotonic() - game["started_at"]) # 게임 소요 시간

        logger.info(
            f"[GameEnd] user={user_id} game={game_id[:8]} mode={game['mode']} "
            f"score={total_score} correct={correct_count}/{total_count} dur={duration_sec}s"
        )

        # 게임 기록 DB 저장 (실패해도 응답은 정상 반환)
        try:
            await self.db.save_game_history(
                user_id     = user_id,
                mode        = game["mode"],
                score       = total_score,
                duration_sec= duration_sec,
                word_results= results,
            )
        except Exception as e:
            logger.error(f"[GameEnd] 게임 기록 저장 실패: {e}", exc_info=True)

        return {
            "type"         : MessageType.RES_GAME_END,
            "status"       : "ok",
            "score"        : total_score,    # NO.406
            "correct_count": correct_count,  # NO.406
            "total_count"  : total_count,    # NO.406
        }
