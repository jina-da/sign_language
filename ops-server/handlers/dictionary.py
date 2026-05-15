"""
handlers/dictionary.py  —  사전 핸들러 (NO.301 ~ 304)

담당 메시지:
  NO.301 REQ_DICT_SEARCH   → NO.302 RES_DICT_SEARCH    정방향 (단어 → 영상)
  NO.303 REQ_DICT_REVERSE  → NO.304 RES_DICT_REVERSE   역방향 (수화 영상 → 단어)

역방향 흐름:
  frames 수신 → NO.501 REQ_AI_INFER → NO.502 RES_AI_INFER
  → predicted_word_id 로 DB 에서 단어/뜻 조회 → RES_DICT_REVERSE 반환
"""

import asyncio
import uuid
import asyncio
import os        # ← 추가
import uuid

from logger import get_logger
from protocol import MessageType, make_error, ErrorCode
from handlers.base import BaseHandler, KEYPOINT_VER

logger = get_logger("handler.dictionary")
VIDEO_SERVER = os.getenv("VIDEO_SERVER_URL", "http://localhost:8000")  # ← 추가


class DictionaryHandler(BaseHandler):

    def __init__(self, db_manager, session_manager, ai_pool, config, learning_handler):
        super().__init__(db_manager, session_manager, ai_pool, config)
        # 역방향 추론에 _request_ai_inference 를 재사용하기 위해 LearningHandler 참조
        self._learning = learning_handler

    # ── NO.301 REQ_DICT_SEARCH → NO.302 RES_DICT_SEARCH ──────

    async def handle_dict_search(self, msg: dict, session_id: str) -> dict:
        """
        단어(query)로 수화 영상 URL 리스트를 찾아 반환한다.
        결과가 여러 개일 경우(동음이의어) 모두 results 배열에 담아 응답함.
        """
        user_id = self._get_user_id(session_id)
        if not user_id:
            return self._not_authenticated()

        query = msg.get("query", "")
        language = msg.get("language", "KSL")
        logger.debug(f"[DictSearch] user={user_id} query='{query}' language={language}")

        try:
            # DB에서 검색된 모든 결과 리스트를 가져옴
            db_results = await self.db.search_word_forward(query)

            # 결과가 없는 경우 처리
            if not db_results:
                logger.debug(f"[DictSearch] user={user_id} query='{query}' 결과 없음")
                return make_error(ErrorCode.WORD_NOT_FOUND, f"word_not_found: {query}")

        except Exception as e:
            logger.error(f"[DictSearch] user={user_id} DB 오류: {e}", exc_info=True)
            return make_error(ErrorCode.SERVER_ERROR, "server_error")

        # 가공된 데이터를 담을 리스트 생성
        results_list = []

        for row in db_results:
            # 파일명 추출 및 CDN URL 생성
            filename = os.path.basename(row["video_path"]) if row.get("video_path") else None
            video_cdn_url = f"{VIDEO_SERVER}/video/{filename}" if filename else None

            # 클라이언트에 보낼 객체 구성
            results_list.append({
                "word_id": row["id"],
                "word": row["word"],
                "word_number": row.get("word_number"),
                "meaning": row.get("meaning"),
                "video_cdn_url": video_cdn_url,
            })

        #단어 보내기 전 로그에 기록
        # 최종 응답: results 키에 리스트를 할당
        word_names = [f"{r['word']}({r['word_number']}) - {r['meaning']}" for r in results_list]
        logger.info(f"[DictSearch] user={user_id} query='{query}' → {len(results_list)}개: {word_names}")

            #배열 구조로 바꿔서 요청한 단어에 대해 모두 전달
        # 최종 응답: results 키에 리스트를 할당
        return {
            "type": MessageType.RES_DICT_SEARCH,
            "status": "ok",
            "results": results_list
        }

    async def handle_dict_reverse(self, msg: dict, session_id: str) -> dict:
        """
        수화 keypoint 시퀀스를 AI 서버에 보내 단어를 추론한다.
        추론된 word_id 로 DB 에서 단어와 뜻을 조회해 반환한다.

        NO.303 요청 필드: keypoint_version, total_frames, frames
        NO.304 응답 필드: word_id, word, description
        """
        user_id = self._get_user_id(session_id)
        if not user_id:
            return self._not_authenticated()

        keypoint_version = msg.get("keypoint_version", KEYPOINT_VER)
        total_frames     = msg.get("total_frames", 0)
        frames           = msg.get("frames", [])

        # frames 유효성 검사
        err = self._validate_frames(frames)
        if err:
            return err

        logger.debug(f"[DictReverse] user={user_id} total_frames={total_frames}")

        # AI 추론 (LearningHandler 의 공통 메서드 재사용)
        request_id       = str(uuid.uuid4())
        model_version_id = await self.db.get_active_model_version_id()

        try:
            ai_result = await asyncio.wait_for(
                self._learning._request_ai_inference(
                    request_id       = request_id,
                    model_version_id = model_version_id,
                    keypoint_version = keypoint_version,
                    total_frames     = total_frames,
                    frames           = frames,
                ),
                timeout=self.config.AI_TIMEOUT,
            )
        except asyncio.TimeoutError:
            return make_error(ErrorCode.AI_TIMEOUT, "AI 추론 시간 초과")
        except Exception as e:
            logger.error(f"[DictReverse] AI 오류: {e}", exc_info=True)
            return make_error(ErrorCode.AI_INFER_ERROR, "ai_error")

        predicted_word_id = ai_result.get("predicted_word_id")
        if predicted_word_id is None:
            return make_error(ErrorCode.WORD_NOT_FOUND, "word_not_found")

        # 예측된 word_id 로 단어 + 뜻 조회
        try:
            word_info = await self.db.get_word_info(predicted_word_id)
        except Exception as e:
            logger.error(f"[DictReverse] 단어 조회 오류: {e}", exc_info=True)
            return make_error(ErrorCode.SERVER_ERROR, "server_error")

        if not word_info:
            return make_error(ErrorCode.WORD_NOT_FOUND, "word_not_found")

        # --- 추천 로그 추가 위치 ---
        # AI가 추론한 ID와 그 ID로 실제 DB에서 찾은 단어명을 매칭하여 출력
        logger.info(
            f"[DictReverse Result] AI 추론 단어 ID: {predicted_word_id} "
            f"| 매칭 단어명: '{word_info['word']}' "
            f"| 신뢰도: {ai_result.get('confidence', 0.0):.4f} "
            f"| user_id: {user_id}"
        )
        # -------------------------

        logger.debug(
            f"[DictReverse] user={user_id} → "
            f"word_id={predicted_word_id} word='{word_info['word']}'"
        )
        return {
            "type"       : MessageType.RES_DICT_REVERSE,
            "status"     : "ok",
            "word_id"    : predicted_word_id,
            "word"       : word_info["word"],
            "meaning": word_info.get("meaning", ""),  # NO.304
        }
