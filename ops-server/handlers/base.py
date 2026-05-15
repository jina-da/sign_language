"""
handlers/base.py

모든 핸들러 클래스가 공통으로 상속하는 BaseHandler.
OpsServer 의 핵심 의존성(db, session, ai_pool, config)을
생성자에서 주입받아 self 로 사용한다.
"""

from config import ServerConfig
from logger import get_logger
from protocol import Protocol, MessageType, make_error, ErrorCode

# 프레임 수 제한 — 모든 핸들러에서 공통으로 참조
MAX_FRAMES   = 300
MIN_FRAMES   = 3
KEYPOINT_VER = "v1"


class BaseHandler:
    def __init__(self, db_manager, session_manager, ai_pool, config: ServerConfig):
        self.db      = db_manager       # DBManager
        self.session = session_manager  # SessionManager
        self.ai_pool = ai_pool          # AIConnectionPool
        self.config  = config           # ServerConfig

    def _get_user_id(self, session_id: str):
        """세션에서 user_id 를 꺼낸다. 없으면 None 반환."""
        return self.session.get_user_id(session_id)

    def _not_authenticated(self):
        """인증 실패 공통 오류 응답"""
        return make_error(ErrorCode.NOT_AUTHENTICATED, "not_authenticated")

    def _validate_frames(self, frames: list):
        """
        frames 리스트 유효성 검사.
        통과하면 None, 실패하면 make_error dict 반환.
        """
        if not isinstance(frames, list) or not frames:
            return make_error(ErrorCode.INVALID_KEYPOINT, "invalid_keypoint_format")
        frame_cnt = len(frames)
        if not (MIN_FRAMES <= frame_cnt <= MAX_FRAMES):
            return make_error(ErrorCode.INVALID_FRAME_COUNT, f"invalid_frame_count:{frame_cnt}")
        return None  # 통과
