"""
handlers/settings.py  —  개인설정 핸들러 (NO.801 ~ 812)

담당 메시지:
  NO.801 REQ_SET_DAILY_GOAL     → NO.802 RES_SET_DAILY_GOAL     하루 목표 단어 수 변경
  NO.803 REQ_SET_DOMINANT_HAND  → NO.804 RES_SET_DOMINANT_HAND  우세손 변경
  NO.805 REQ_SET_DEAF           → NO.806 RES_SET_DEAF            농인 여부 변경
  NO.807 REQ_CHANGE_PASSWORD    → NO.808 RES_CHANGE_PASSWORD     비밀번호 변경
  NO.809 REQ_SET_CONSENT        → NO.810 RES_SET_CONSENT         키포인트 수집 동의 변경
  NO.811 REQ_WITHDRAW           → NO.812 RES_WITHDRAW            회원 탈퇴
"""

from logger import get_logger
from protocol import MessageType, make_error, ErrorCode
from handlers.base import BaseHandler

logger = get_logger("handler.settings")


class SettingsHandler(BaseHandler):

    # ── NO.801 REQ_SET_DAILY_GOAL → NO.802 RES_SET_DAILY_GOAL ──

    async def handle_set_daily_goal(self, msg: dict, session_id: str) -> dict:
        """
        하루 학습 목표 단어 수를 변경한다.
        NO.801 요청 필드: daily_goal (int, 5~30)
        NO.802 응답 필드: status, daily_goal
        """
        user_id = self._get_user_id(session_id)
        if not user_id:
            return self._not_authenticated()

        daily_goal = msg.get("daily_goal")

        # 유효성 검사: 5~30 정수
        if not isinstance(daily_goal, int) or not (5 <= daily_goal <= 30):
            logger.warning(f"[Settings] user={user_id} 잘못된 daily_goal={daily_goal!r}")
            return make_error(ErrorCode.SERVER_ERROR, "daily_goal must be integer between 5 and 30")

        try:
            await self.db.update_user_field(user_id, "daily_goal", daily_goal)
            logger.info(f"[Settings] user={user_id} daily_goal={daily_goal} 변경 완료")
            return {
                "type"      : MessageType.RES_SET_DAILY_GOAL,
                "status"    : "ok",
                "daily_goal": daily_goal,
            }
        except Exception as e:
            logger.error(f"[Settings] user={user_id} daily_goal 변경 실패: {e}", exc_info=True)
            return make_error(ErrorCode.SERVER_ERROR, "server_error")

    # ── NO.803 REQ_SET_DOMINANT_HAND → NO.804 RES_SET_DOMINANT_HAND ──

    async def handle_set_dominant_hand(self, msg: dict, session_id: str) -> dict:
        """
        우세손을 변경한다.
        NO.803 요청 필드: dominant_hand ("left" / "right")
        NO.804 응답 필드: status, dominant_hand
        """
        user_id = self._get_user_id(session_id)
        if not user_id:
            return self._not_authenticated()

        dominant_hand = msg.get("dominant_hand")

        if dominant_hand not in ("left", "right"):
            logger.warning(f"[Settings] user={user_id} 잘못된 dominant_hand={dominant_hand!r}")
            return make_error(ErrorCode.SERVER_ERROR, "dominant_hand must be 'left' or 'right'")

        is_dominant_left = (dominant_hand == "left")

        try:
            await self.db.update_user_field(user_id, "is_dominant_left", is_dominant_left)
            logger.info(f"[Settings] user={user_id} dominant_hand={dominant_hand} 변경 완료")
            return {
                "type"         : MessageType.RES_SET_DOMINANT_HAND,
                "status"       : "ok",
                "dominant_hand": dominant_hand,
            }
        except Exception as e:
            logger.error(f"[Settings] user={user_id} dominant_hand 변경 실패: {e}", exc_info=True)
            return make_error(ErrorCode.SERVER_ERROR, "server_error")

    # ── NO.805 REQ_SET_DEAF → NO.806 RES_SET_DEAF ──────────────

    async def handle_set_deaf(self, msg: dict, session_id: str) -> dict:
        """
        농인 여부를 변경한다.
        NO.805 요청 필드: is_deaf (bool)
        NO.806 응답 필드: status, is_deaf
        """
        user_id = self._get_user_id(session_id)
        if not user_id:
            return self._not_authenticated()

        is_deaf = msg.get("is_deaf")

        if not isinstance(is_deaf, bool):
            logger.warning(f"[Settings] user={user_id} 잘못된 is_deaf={is_deaf!r}")
            return make_error(ErrorCode.SERVER_ERROR, "is_deaf must be boolean")

        try:
            await self.db.update_user_field(user_id, "is_deaf", is_deaf)
            logger.info(f"[Settings] user={user_id} is_deaf={is_deaf} 변경 완료")
            return {
                "type"   : MessageType.RES_SET_DEAF,
                "status" : "ok",
                "is_deaf": is_deaf,
            }
        except Exception as e:
            logger.error(f"[Settings] user={user_id} is_deaf 변경 실패: {e}", exc_info=True)
            return make_error(ErrorCode.SERVER_ERROR, "server_error")

    # ── NO.807 REQ_CHANGE_PASSWORD → NO.808 RES_CHANGE_PASSWORD ──

    async def handle_change_password(self, msg: dict, session_id: str) -> dict:
        """
        비밀번호를 변경한다.
        현재 비밀번호를 확인한 후 새 비밀번호로 갱신한다.
        NO.807 요청 필드: current_password (SHA-256 해시), new_password (SHA-256 해시)
        NO.808 응답 필드: status
        """
        user_id = self._get_user_id(session_id)
        if not user_id:
            return self._not_authenticated()

        current_password = msg.get("current_password", "")
        new_password     = msg.get("new_password", "")

        if not current_password or not new_password:
            return make_error(ErrorCode.SERVER_ERROR, "password fields required")

        try:
            # 현재 비밀번호 확인
            is_valid = await self.db.verify_password(user_id, current_password)
            if not is_valid:
                logger.warning(f"[Settings] user={user_id} 현재 비밀번호 불일치")
                return make_error(ErrorCode.INVALID_CREDENTIALS, "invalid_current_password")

            # 새 비밀번호로 갱신
            await self.db.update_user_field(user_id, "password_hash", new_password)
            logger.info(f"[Settings] user={user_id} 비밀번호 변경 완료")
            return {
                "type"  : MessageType.RES_CHANGE_PASSWORD,
                "status": "ok",
            }
        except Exception as e:
            logger.error(f"[Settings] user={user_id} 비밀번호 변경 실패: {e}", exc_info=True)
            return make_error(ErrorCode.SERVER_ERROR, "server_error")

    # ── NO.809 REQ_SET_CONSENT → NO.810 RES_SET_CONSENT ────────

    async def handle_set_consent(self, msg: dict, session_id: str) -> dict:
        """
        키포인트 데이터 수집 동의 여부를 변경한다.
        NO.809 요청 필드: keypoint_consent (bool)
        NO.810 응답 필드: status, keypoint_consent
        """
        user_id = self._get_user_id(session_id)
        if not user_id:
            return self._not_authenticated()

        keypoint_consent = msg.get("keypoint_consent")

        if not isinstance(keypoint_consent, bool):
            logger.warning(f"[Settings] user={user_id} 잘못된 keypoint_consent={keypoint_consent!r}")
            return make_error(ErrorCode.SERVER_ERROR, "keypoint_consent must be boolean")

        try:
            await self.db.update_user_field(user_id, "keypoint_consent", keypoint_consent)
            logger.info(f"[Settings] user={user_id} keypoint_consent={keypoint_consent} 변경 완료")
            return {
                "type"            : MessageType.RES_SET_CONSENT,
                "status"          : "ok",
                "keypoint_consent": keypoint_consent,
            }
        except Exception as e:
            logger.error(f"[Settings] user={user_id} keypoint_consent 변경 실패: {e}", exc_info=True)
            return make_error(ErrorCode.SERVER_ERROR, "server_error")

    # ── NO.811 REQ_WITHDRAW → NO.812 RES_WITHDRAW ───────────────

    async def handle_withdraw(self, msg: dict, session_id: str) -> dict:
        """
        회원 탈퇴 처리. 비밀번호 확인 후 is_active = 0 으로 Soft delete.
        NO.811 요청 필드: password (SHA-256 해시)
        NO.812 응답 필드: status
        """
        user_id = self._get_user_id(session_id)
        if not user_id:
            return self._not_authenticated()

        password = msg.get("password", "")

        if not password:
            return make_error(ErrorCode.SERVER_ERROR, "password required")

        try:
            # 비밀번호 확인
            is_valid = await self.db.verify_password(user_id, password)
            if not is_valid:
                logger.warning(f"[Settings] user={user_id} 탈퇴 — 비밀번호 불일치")
                return make_error(ErrorCode.INVALID_CREDENTIALS, "invalid_password")

            # is_active = 0 으로 Soft delete
            await self.db.update_user_field(user_id, "is_active", 0)
            logger.info(f"[Settings] user={user_id} 회원 탈퇴 완료 (Soft delete)")

            return {
                "type"  : MessageType.RES_WITHDRAW,
                "status": "ok",
            }
        except Exception as e:
            logger.error(f"[Settings] user={user_id} 회원 탈퇴 실패: {e}", exc_info=True)
            return make_error(ErrorCode.SERVER_ERROR, "server_error")