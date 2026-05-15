"""
SignLearn 세션 관리자 - 디버그 로깅 강화본
변경 내용:
  ✅ logger.py 중앙 로거 사용
  ✅ 세션 생성/만료/강제종료 모두 로그
  ✅ 한도 초과 시 현재 세션 수 표시
  ✅ 만료 정리 시 각 세션의 주소·지속시간 로그
"""

import asyncio
import time
import uuid
from typing import Optional

from logger import get_logger

logger = get_logger("session_manager")

SESSION_TTL      = 7200   # 2시간
CLEANUP_INTERVAL = 60     # 60초마다 만료 체크
MAX_SESSIONS     = 200

class Session:
    __slots__ = ("session_id", "addr", "user_id", "created_at", "last_active", "word_count")

    def __init__(self, session_id: str, addr):
        self.session_id  = session_id
        self.addr        = addr
        self.user_id: Optional[int] = None
        self.created_at  = time.monotonic()
        self.last_active = time.monotonic()
        self.word_count  = 0

    def touch(self):
        self.last_active = time.monotonic()

    def is_expired(self) -> bool:
        return (time.monotonic() - self.last_active) > SESSION_TTL

    def idle_sec(self) -> int:
        return int(time.monotonic() - self.last_active)

    def duration_sec(self) -> int:
        return int(time.monotonic() - self.created_at)

class SessionManager:
    def __init__(self):
        self._sessions: dict[str, Session] = {}
        self._lock = asyncio.Lock()
        self._cleanup_task: Optional[asyncio.Task] = None

    async def start(self):
        self._cleanup_task = asyncio.create_task(self._cleanup_loop())
        logger.info(f"[Session] 세션 관리자 시작 (TTL={SESSION_TTL}s, 최대={MAX_SESSIONS})")

    async def stop(self):
        if self._cleanup_task:
            self._cleanup_task.cancel()
            try:
                await self._cleanup_task
            except asyncio.CancelledError:
                pass
        logger.info("[Session] 세션 관리자 종료")

    async def create(self, addr) -> Optional[str]:
        async with self._lock:
            current = len(self._sessions)
            if current >= MAX_SESSIONS:
                logger.warning(
                    f"[Session] 세션 한도 초과 — 현재={current}/{MAX_SESSIONS} "
                    f"addr={addr} 연결 거절"
                )
                return None
            session_id = str(uuid.uuid4())
            self._sessions[session_id] = Session(session_id, addr)
            logger.debug(
                f"[Session] 생성: sid={session_id[:8]} addr={addr} "
                f"현재세션수={current + 1}"
            )
        return session_id

    def bind_user(self, session_id: str, user_id: int):
        session = self._sessions.get(session_id)
        if session:
            old_uid = session.user_id
            session.user_id = user_id
            session.touch()
            logger.debug(
                f"[Session] user 바인딩: sid={session_id[:8]} "
                f"user_id={user_id} (이전={old_uid})"
            )
        else:
            logger.warning(f"[Session] bind_user 실패 — 존재하지 않는 sid={session_id[:8]}")

    def get_user_id(self, session_id: str) -> Optional[int]:
        session = self._sessions.get(session_id)
        if session:
            session.touch()
            return session.user_id
        logger.warning(f"[Session] get_user_id — sid={session_id[:8]} 세션 없음")
        return None

    def get(self, session_id: str, user_id: int = None) -> Optional[Session]:
        """세션 객체 반환. 없으면 None."""
        return self._sessions.get(session_id)

    # ← 여기에 추가
    async def restore(self, session_id: str, addr=None, user_id=None):
        """로그아웃 후 재로그인 시 세션을 복구한다."""
        async with self._lock:
            if session_id not in self._sessions:
                self._sessions[session_id] = Session(session_id, addr)
                logger.debug(f"[Session] 복구: sid={session_id[:8]}")

    async def remove(self, session_id: str, db_manager):
        async with self._lock:
            session = self._sessions.pop(session_id, None)

        if session is None:
            logger.debug(f"[Session] remove 호출됐으나 이미 없는 sid={session_id[:8]}")
            return

        logger.info(
            f"[Session] 종료: sid={session_id[:8]} user={session.user_id} "
            f"addr={session.addr} 지속={session.duration_sec()}s 단어={session.word_count}"
        )

        if session.user_id:
            try:
                await db_manager.close_session(
                    session_id=session_id,
                    word_count=session.word_count,
                    duration_sec=session.duration_sec(),
                )
            except Exception as e:
                logger.error(
                    f"[Session] DB 세션 기록 실패 sid={session_id[:8]}: "
                    f"{type(e).__name__}: {e}",
                    exc_info=True
                )

    async def _cleanup_loop(self):
        logger.debug(f"[Session] 만료 정리 루프 시작 (주기={CLEANUP_INTERVAL}s)")
        while True:
            await asyncio.sleep(CLEANUP_INTERVAL)
            async with self._lock:
                expired = [
                    (sid, s)
                    for sid, s in self._sessions.items()
                    if s.is_expired()
                ]
                for sid, s in expired:
                    del self._sessions[sid]
                    logger.info(
                        f"[Session] 만료 정리: sid={sid[:8]} user={s.user_id} "
                        f"addr={s.addr} 유휴={s.idle_sec()}s 지속={s.duration_sec()}s"
                    )

            if expired:
                logger.info(
                    f"[Session] 만료 정리 완료: {len(expired)}개 제거, "
                    f"남은 세션={len(self._sessions)}"
                )

    @property
    def active_count(self) -> int:
        return len(self._sessions)
