"""
handlers/auth.py  —  인증 핸들러 (NO.101 ~ 106)

담당 메시지:
  NO.101 REQ_LOGIN    → NO.102 RES_LOGIN
  NO.103 REQ_LOGOUT   → NO.104 RES_LOGOUT
  NO.105 REQ_REGISTER → NO.106 RES_REGISTER
"""

from logger import get_logger
from protocol import MessageType, make_error, ErrorCode
from handlers.base import BaseHandler

logger = get_logger("handler.auth")


class AuthHandler(BaseHandler):

    # ── NO.101 REQ_LOGIN → NO.102 RES_LOGIN ──────────────────

    async def handle_login(self, msg: dict, session_id: str) -> dict:
        """
        클라이언트가 username + password(SHA-256 해시)를 보내면
        DB 에서 인증 후 session_token 과 현재 model_version_id 를 반환한다.
        """
        username = msg.get("username", "")
        logger.info(f"[Login] 로그인 시도: username='{username}' sid={session_id[:8]}")

        # DB 에서 자격증명 확인
        try:
            user = await self.db.get_user_by_credentials(username, msg.get("password", ""))
        except Exception as e:
            logger.error(f"[Login] DB 조회 오류: {type(e).__name__}: {e}", exc_info=True)
            return make_error(ErrorCode.SERVER_ERROR, "server_error")

        if not user:
            logger.warning(f"[Login] 인증 실패: username='{username}'")
            return make_error(ErrorCode.INVALID_CREDENTIALS, "invalid_credentials")

        # ── 세션이 이미 제거된 경우 재생성 ──────────────────
        await self.session.restore(session_id)  # 세션 없으면 복구, 있으면 무시
        if not self.session.get(session_id, user["id"]):
            await self.session.restore(session_id, user["id"])  # 인메모리 세션 복구
        # ────────────────────────────────────────────────────

        # 세션에 user_id 바인딩
        self.session.bind_user(session_id, user["id"])
        try:
            await self.db.upsert_session(session_id, user["id"])
        except Exception as e:
            # DB 세션 기록 실패는 치명적이지 않으므로 경고만 남기고 계속
            logger.warning(f"[Login] 세션 DB 기록 실패 (로그인은 성공): {e}")

        # NO.102: 현재 활성 모델 버전을 함께 반환
        model_version_id = await self.db.get_active_model_version_id()

        review_pending_count = await self.db.get_review_pending_count(user["id"])
        high_score = await self.db.get_high_score(user["id"])

        logger.info(f"[Login] 로그인 성공: user_id={user['id']} username='{username}'")
        return {
            "type": MessageType.RES_LOGIN,
            "status": "ok",
            "session_token": session_id,
            "user_id": user["id"],
            "model_version_id": model_version_id,
            "dominant_hand": "left" if user["is_dominant_left"] else "right",  # ← 우세손 추가
            "is_deaf": bool(user["is_deaf"]),
            "keypoint_consent": bool(user["keypoint_consent"]),
            "daily_goal": user["daily_goal"],
            "review_pending_count": review_pending_count,  # ← 추가
            "high_score": high_score,  # ← 빠진 필드 추가
        }

    # ── NO.103 REQ_LOGOUT → NO.104 RES_LOGOUT ────────────────

    async def handle_logout(self, _msg: dict, session_id: str) -> dict:
        """세션을 제거하고 DB 세션 레코드를 종료한다."""
        user_id = self._get_user_id(session_id)
        logger.info(f"[Logout] user={user_id} sid={session_id[:8]} 로그아웃")

        await self.session.remove(session_id, self.db)

        return {
            "type"  : MessageType.RES_LOGOUT,
            "status": "ok",
        }
    # ── NO.105 REQ_REGISTER → NO.106 RES_REGISTER ────────────

    async def handle_register(self, msg: dict, session_id: str) -> dict:
        """
        신규 사용자를 등록한다.
        NO.105 명세: dominant_hand 는 "right" / "left" 문자열로 수신.
        DB 저장 시 is_dominant_left(bool) 로 변환한다.
        """
        username = msg.get("username", "")
        logger.info(f"[Register] 회원가입 시도: username='{username}'")

        # dominant_hand 문자열 → is_dominant_left bool 변환
        dominant_hand    = msg.get("dominant_hand", "right")
        is_dominant_left = (dominant_hand == "left")

        try:
            user_id = await self.db.create_user(
                username        = username,
                password_hash   = msg.get("password", ""),
                is_deaf         = msg.get("is_deaf", False),
                is_dominant_left= is_dominant_left,
                keypoint_consent= msg.get("keypoint_consent", False),
            )
            logger.info(
                f"[Register] 회원가입 성공: user_id={user_id} username='{username}' "
                f"dominant_hand={dominant_hand} "
                f"keypoint_consent={msg.get('keypoint_consent', False)}"
            )
            return {
                "type"   : MessageType.RES_REGISTER,
                "status" : "ok",
                "user_id": user_id,  # NO.106
            }
        except Exception as e:
            logger.error(
                f"[Register] 회원가입 실패: username='{username}': "
                f"{type(e).__name__}: {e}",
                exc_info=True
            )
            return make_error(ErrorCode.USERNAME_TAKEN, str(e))
