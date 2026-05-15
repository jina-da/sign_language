"""
SignLearn 운용 서버 (Operation Server)

이 파일은 서버 골격만 담당한다.
실제 메시지 처리는 handlers/ 패키지의 핸들러 클래스에 위임한다.

  handlers/auth.py        AuthHandler       NO.101~106
  handlers/learning.py    LearningHandler   NO.201~206, NO.501~502
  handlers/dictionary.py  DictionaryHandler NO.301~304
  handlers/game.py        GameHandler       NO.401~406
  handlers/health.py      HealthHandler     NO.701~702
  handlers/settings.py    SettingsHandler   NO.801~812


OpsServer 가 담당하는 것:
  - 서버 생명주기 (start / cleanup / shutdown)
  - 클라이언트 연결 수락 및 메시지 루프
  - _route_message: type 별로 핸들러에 위임
  - AIConnectionPool: AI 서버 연결 풀 관리
  - DB 저장 재시도 워커
  - 통계 리포터
"""

import asyncio
import signal
import time
from contextlib import asynccontextmanager
from typing import Optional

from config import ServerConfig
from logger import get_logger
from protocol import Protocol, MessageType, make_error, ErrorCode
from db_manager_fixed import DBManager
from model_manager import ModelManager
from session_manager_fixed import SessionManager
from retrain_scheduler import RetrainScheduler

# 핸들러 임포트
from handlers.auth       import AuthHandler
from handlers.learning   import LearningHandler
from handlers.dictionary import DictionaryHandler
from handlers.game       import GameHandler
from handlers.health     import HealthHandler
from handlers.settings import SettingsHandler

logger = get_logger("ops_server")

# ──────────────────────────────────────────────────────────────
# AI 연결 풀
# ──────────────────────────────────────────────────────────────

class AIConnectionPool:
    """
    AI 서버와의 TCP 연결을 미리 열어 두고 asyncio.Queue 로 관리한다.
    추론 요청마다 새 연결을 여는 비용을 없애기 위해 사용한다.
    acquire() 는 asynccontextmanager 로 구현해 with 블록이 끝나면
    연결을 자동으로 반환(또는 재연결 후 반환)한다.
    """

    def __init__(self, host: str, port: int, size: int = 4,reconnect_attempts: int = 3):
        self._host  = host
        self._port  = port
        self._size  = size
        self._reconnect_attempts = reconnect_attempts  # ← 이 줄 추가
        self._queue: asyncio.Queue = asyncio.Queue(maxsize=size)
        self._total_requests  = 0
        self._failed_requests = 0

    async def init(self):
        """서버 기동 시 size 개의 연결을 미리 생성한다."""
        logger.info(f"[AIPool] 초기화 시작: {self._host}:{self._port} 연결 {self._size}개")
        ok = 0
        for i in range(self._size):
            conn = await self._connect(slot=i)
            if conn is None:
                conn = (None, None)
            await self._queue.put(conn)
            if conn[0] is not None:
                ok += 1
        logger.info(f"[AIPool] 초기화 완료: {ok}/{self._size}개 연결 성공")
        if ok == 0:
            logger.critical("[AIPool] AI 서버 연결 0개 — 추론 불가 상태로 기동됨")

    async def _connect(self, slot: int = -1) -> tuple:
        tag = f"slot={slot}" if slot >= 0 else "재연결"
        for attempt in range(1, self._reconnect_attempts + 1):
            try:
                reader, writer = await asyncio.open_connection(self._host, self._port)
                logger.debug(f"[AIPool] {tag} 연결 성공 (시도 {attempt}/{self._reconnect_attempts})")
                return (reader, writer)
            except asyncio.CancelledError:
                logger.debug(f"[AIPool] {tag} 연결 취소됨 (서버 종료)")
                return (None, None)  # ← 추가
            except Exception as e:
                wait = 2 ** attempt
                logger.warning(
                    f"[AIPool] {tag} 연결 실패 ({attempt}/{self._reconnect_attempts}): "
                    f"{type(e).__name__}: {e} → {wait}초 후 재시도"
                )
                try:
                    await asyncio.sleep(wait)
                except asyncio.CancelledError:
                    logger.debug(f"[AIPool] {tag} 재시도 대기 중 취소됨 (서버 종료)")
                    return (None, None)

    @asynccontextmanager
    async def acquire(self, timeout: float = 2.0):
        """
        큐에서 연결을 꺼낸다. timeout 초 내에 꺼내지 못하면 RuntimeError.
        with 블록이 정상 종료되면 연결을 큐에 반환.
        예외 발생 시 재연결 후 반환한다.
        """
        self._total_requests += 1
        req_id = self._total_requests
        t0 = time.monotonic()

        try:
            conn = await asyncio.wait_for(self._queue.get(), timeout=timeout)
        except asyncio.TimeoutError:
            self._failed_requests += 1
            logger.error(
                f"[AIPool] req#{req_id} 풀 획득 타임아웃 ({timeout}s) — "
                f"총 요청={self._total_requests} 실패={self._failed_requests}"
            )
            raise RuntimeError("AI 연결 풀 획득 타임아웃 - 서버 과부하")

            # ── 여기 추가: (None, None) 슬롯이면 재연결 시도 ──────────
        reader, writer = conn
        if reader is None or writer is None:
            logger.warning(f"[AIPool] req#{req_id} 슬롯이 None — 재연결 시도")
            conn = await self._connect()
            reader, writer = conn
            if reader is None or writer is None:
                await self._queue.put(conn)  # 실패해도 슬롯 유지
                raise RuntimeError("AI 서버 재연결 실패 — 서버 꺼짐 의심")
            logger.info(f"[AIPool] req#{req_id} 재연결 성공")

        wait_ms = int((time.monotonic() - t0) * 1000)
        logger.debug(
            f"[AIPool] req#{req_id} 연결 획득 "
            f"(대기 {wait_ms}ms, 남은 풀={self._queue.qsize()})"
        )

        try:
            yield conn
            # 정상 종료: 연결을 그대로 반환
            await self._queue.put(conn)
            logger.debug(
                f"[AIPool] req#{req_id} 연결 반환 (남은 풀={self._queue.qsize()})"
            )
        except Exception as e:
            self._failed_requests += 1
            logger.warning(
                f"[AIPool] req#{req_id} 연결 오류({type(e).__name__}) — 재연결 시도",
                exc_info=True
            )
            # 오류 발생: 새 연결로 교체 후 반환
            new_conn = await self._connect()
            await self._queue.put(new_conn)
            raise

    async def close(self):
        """서버 종료 시 모든 연결을 닫는다."""
        logger.info("[AIPool] 풀 종료 시작")
        closed = 0
        while not self._queue.empty():
            reader, writer = await self._queue.get()
            if writer:
                writer.close()
                try:
                    await writer.wait_closed()
                except Exception:
                    pass
                closed += 1
        logger.info(f"[AIPool] 풀 종료 완료: {closed}개 연결 해제")

# ──────────────────────────────────────────────────────────────
# 운용 서버
# ──────────────────────────────────────────────────────────────

class OpsServer:

    def __init__(self, config: ServerConfig):
        self.config = config

        # ── 공유 인프라 ────────────────────────────────────────
        self.db_manager        = DBManager(config)
        self.model_manager     = ModelManager(config)
        self.session_manager   = SessionManager()
        self.retrain_scheduler = RetrainScheduler(
            config, self.db_manager, self.model_manager
        )
        self._ai_pool = AIConnectionPool(
            config.AI_HOST,
            config.AI_PORT,
            size=4,
            reconnect_attempts=config.AI_RECONNECT_ATTEMPTS  # ← config 값 연결
        )
        self._client_semaphore = asyncio.Semaphore(config.MAX_CLIENTS)
        self._active_tasks: set        = set()
        self._save_retry_queue: asyncio.Queue = asyncio.Queue(maxsize=500)
        self._shutdown_event   = asyncio.Event()

        # ── 게임 상태 (GameHandler 와 공유) ───────────────────
        # {game_id: {user_id, mode, words, results, started_at}}
        self._active_games: dict = {}

        # ── 통계 카운터 ────────────────────────────────────────
        self._stat_conn_total   = 0
        self._stat_conn_refused = 0

        # ── 핸들러 조립 ────────────────────────────────────────
        # LearningHandler 는 AI 추론 공통 메서드를 가지므로 먼저 생성,
        # Dictionary / Game 핸들러가 이를 참조한다.
        self._auth    = AuthHandler(
            self.db_manager, self.session_manager,
            self._ai_pool, config,
        )
        self._learning = LearningHandler(
            self.db_manager, self.session_manager,
            self._ai_pool, config,
            track_task_fn    = self._track_task,
            save_retry_queue = self._save_retry_queue,
        )
        self._dictionary = DictionaryHandler(
            self.db_manager, self.session_manager,
            self._ai_pool, config,
            learning_handler = self._learning,  # AI 추론 메서드 재사용
        )
        self._game = GameHandler(
            self.db_manager, self.session_manager,
            self._ai_pool, config,
            active_games     = self._active_games,  # 게임 상태 공유
            learning_handler = self._learning,       # AI 추론 메서드 재사용
        )
        self._health = HealthHandler(
            self.db_manager, self.session_manager,
            self._ai_pool, config,
        )

        self._settings = SettingsHandler(
            self.db_manager, self.session_manager,
            self._ai_pool, config,
        )

    def _track_task(self, coro) -> asyncio.Task:
        """
        코루틴을 Task 로 등록하고 _active_tasks 에 추가한다.
        종료 시 cancel() 을 통해 모든 태스크를 정리할 수 있다.
        """
        task = asyncio.create_task(coro)
        self._active_tasks.add(task)
        task.add_done_callback(self._active_tasks.discard)
        return task

    # ── 서버 생명주기 ──────────────────────────────────────────
    async def start(self):
        logger.info("=" * 60)
        logger.info("SignLearn 운용 서버 기동")
        logger.info(f"  HOST={self.config.HOST}  PORT={self.config.CLIENT_PORT}")
        logger.info(f"  AI_HOST={self.config.AI_HOST}  AI_PORT={self.config.AI_PORT}")
        logger.info(f"  MAX_CLIENTS={self.config.MAX_CLIENTS}")
        logger.info(f"  AI_TIMEOUT={self.config.AI_TIMEOUT}s")
        logger.info("=" * 60)

        # DB 연결 풀 초기화 — 실패 시 서버 기동 불가
        try:
            await self.db_manager.init_pool()
            logger.info("[Server] DB 연결 풀 초기화 완료")

        except Exception as e:
            logger.critical(f"[Server] DB 초기화 실패 — 기동 불가: {e}", exc_info=True)
            raise

        await self._ai_pool.init()
        logger.info("[Server] AI 연결 풀 초기화 완료")

        # 백그라운드 태스크 시작
        self._track_task(self.retrain_scheduler.run())
        logger.info("[Server] 재학습 스케줄러 시작")

        self._track_task(self._save_retry_worker())
        logger.info("[Server] DB 저장 재시도 워커 시작")

        self._track_task(self._stats_reporter())
        logger.info("[Server] 통계 리포터 시작 (5분 주기)")

        # 클라이언트 수신 시작
        server = await asyncio.start_server(
            self._handle_client,
            self.config.HOST,
            self.config.CLIENT_PORT,
        )
        logger.info(f"[Server] 클라이언트 대기 중: {self.config.HOST}:{self.config.CLIENT_PORT}")

        async with server:
            await self._shutdown_event.wait()  # SIGINT/SIGTERM 까지 블록

        await self._cleanup()

    async def _cleanup(self):
        """종료 시 모든 태스크를 cancel 하고 연결 풀을 닫는다."""
        logger.info(f"[Server] 종료 시작 — 활성 태스크={len(self._active_tasks)}개")
        for task in list(self._active_tasks):
            task.cancel()
        await asyncio.gather(*self._active_tasks, return_exceptions=True)
        await self.db_manager.close_pool()
        await self._ai_pool.close()
        logger.info("[Server] 모든 자원 정리 완료 — 종료")

    def shutdown(self):
        """SIGINT / SIGTERM 핸들러에서 호출한다."""
        logger.info("[Server] 종료 신호 수신 (SIGINT/SIGTERM)")
        self._shutdown_event.set()

    async def _stats_reporter(self):
        """5분마다 서버 주요 지표를 로그에 출력한다."""
        while True:
            await asyncio.sleep(300)
            logger.info(
                f"[Stats] 접속: 총={self._stat_conn_total} 거절={self._stat_conn_refused} | "
                f"추론: 성공={self._learning.stat_ok} 실패={self._learning.stat_err} | "
                f"세션: 현재={self.session_manager.active_count} | "
                f"활성게임: {len(self._active_games)}개 | "
                f"태스크: {len(self._active_tasks)}개 | "
                f"재시도큐: {self._save_retry_queue.qsize()}개"
            )

    # ── 클라이언트 연결 ─────────────────────────────────────────

    async def _handle_client(
        self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter
    ):
        """
        클라이언트 접속 콜백.
        세마포어로 동시 접속 수를 제한하고,
        세션을 생성한 뒤 _client_loop 에 위임한다.
        """
        addr = writer.get_extra_info("peername")
        self._stat_conn_total += 1

        # 동시 접속 한도 초과 시 즉시 거절
        if self._client_semaphore.locked():
            self._stat_conn_refused += 1
            logger.warning(
                f"[Client] {addr} 동시 접속 초과 — 거절 "
                f"(총={self._stat_conn_total} 거절={self._stat_conn_refused})"
            )
            writer.close()
            return

        async with self._client_semaphore:
            session_id = None
            connect_ts = time.monotonic()
            try:
                session_id = await self.session_manager.create(addr)
                if session_id is None:
                    logger.warning(f"[Client] {addr} 세션 생성 실패(한도 초과) — 연결 거절")
                    writer.close()
                    return

                logger.info(f"[Client] {addr} 연결 | session={session_id[:8]}...")
                await self._client_loop(reader, writer, addr, session_id)

            except asyncio.CancelledError:
                logger.debug(
                    f"[Client] {addr} session="
                    f"{session_id[:8] if session_id else '?'} 태스크 취소됨"
                )
            except Exception as e:
                logger.error(
                    f"[Client] {addr} session="
                    f"{session_id[:8] if session_id else '?'} "
                    f"처리 중 예외: {type(e).__name__}: {e}",
                    exc_info=True
                )
            finally:
                duration = int(time.monotonic() - connect_ts)
                if session_id:
                    await self.session_manager.remove(session_id, self.db_manager)
                try:
                    writer.close()
                    await writer.wait_closed()
                except Exception as e:
                    logger.debug(f"[Client] {addr} 소켓 닫기 오류(무시): {e}")
                logger.info(
                    f"[Client] {addr} 연결 종료 | "
                    f"session={session_id[:8] if session_id else '?'}... | "
                    f"접속시간={duration}s"
                )

    async def _client_loop(self, reader, writer, addr, session_id: str):
        """
        메시지를 반복 수신하고 _route_message 로 분기한다.
        CLIENT_TIMEOUT 동안 메시지가 없으면 연결을 끊는다.
        """
        sid_short = session_id[:8]
        msg_count = 0

        while True:
            try:
                msg = await asyncio.wait_for(
                    Protocol.recv_message(reader),
                    timeout=self.config.CLIENT_TIMEOUT,
                )
            except asyncio.TimeoutError:
                logger.info(
                    f"[Client] {addr} sid={sid_short} "
                    f"유휴 타임아웃({self.config.CLIENT_TIMEOUT}s) — 연결 종료"
                )
                break
            except asyncio.IncompleteReadError:
                logger.debug(f"[Client] {addr} sid={sid_short} 연결 끊김 (IncompleteRead)")
                break
            except Exception as e:
                logger.error(
                    f"[Client] {addr} sid={sid_short} "
                    f"수신 오류: {type(e).__name__}: {e}",
                    exc_info=True
                )
                break

            if msg is None:
                logger.debug(f"[Client] {addr} sid={sid_short} EOF — 루프 종료")
                break

            msg_count += 1
            msg_type = msg.get("type", "UNKNOWN")
            logger.debug(
                f"[Client] {addr} sid={sid_short} "
                f"msg#{msg_count} type={msg_type} 수신"
            )

            t0 = time.monotonic()
            response = await self._route_message(msg_type, msg, session_id)
            elapsed_ms = int((time.monotonic() - t0) * 1000)

            if response:
                resp_type = response.get("type", "?")
                logger.debug(
                    f"[Client] {addr} sid={sid_short} msg#{msg_count} "
                    f"{msg_type} → {resp_type} ({elapsed_ms}ms)"
                )
                try:
                    await Protocol.send_message(writer, response)
                except Exception as e:
                    logger.error(
                        f"[Client] {addr} sid={sid_short} 응답 전송 실패: {e}",
                        exc_info=True
                    )
                    break

    # ── 메시지 라우터 ──────────────────────────────────────────

    async def _route_message(
        self, msg_type: str, msg: dict, session_id: str
    ) -> Optional[dict]:
        """
        수신된 type 문자열을 각 핸들러 메서드에 매핑한다.
        핸들러가 없으면 NO.703 RES_ERROR 를 반환한다.
        """
        handlers = {
            # ── 인증 (handlers/auth.py) ───────────────────────
            MessageType.REQ_LOGIN   : self._auth.handle_login,
            MessageType.REQ_LOGOUT  : self._auth.handle_logout,
            MessageType.REQ_REGISTER: self._auth.handle_register,

            # ── 학습 / 추론 (handlers/learning.py) ───────────
            MessageType.REQ_DAILY_WORDS : self._learning.handle_daily_words,
            MessageType.REQ_INFER       : self._learning.handle_infer,
            MessageType.REQ_REVIEW_WORDS: self._learning.handle_review_words,

            # ── 사전 (handlers/dictionary.py) ─────────────────
            MessageType.REQ_DICT_SEARCH : self._dictionary.handle_dict_search,
            MessageType.REQ_DICT_REVERSE: self._dictionary.handle_dict_reverse,

            # ── 게임 (handlers/game.py) ───────────────────────
            MessageType.REQ_GAME_START: self._game.handle_game_start,
            MessageType.REQ_GAME_INFER: self._game.handle_game_infer,
            MessageType.REQ_GAME_END  : self._game.handle_game_end,

            # ── 헬스체크 (handlers/health.py) ─────────────────
            MessageType.REQ_PING: self._health.handle_ping,

            # ── 개인설정 (settings/settings.py) ─────────────────
            MessageType.REQ_SET_DAILY_GOAL: self._settings.handle_set_daily_goal,
            MessageType.REQ_SET_DOMINANT_HAND: self._settings.handle_set_dominant_hand,
            MessageType.REQ_SET_DEAF: self._settings.handle_set_deaf,
            MessageType.REQ_CHANGE_PASSWORD: self._settings.handle_change_password,
            MessageType.REQ_SET_CONSENT: self._settings.handle_set_consent,
            MessageType.REQ_WITHDRAW: self._settings.handle_withdraw,
        }

        handler = handlers.get(msg_type)
        if not handler:
            logger.warning(f"[Route] 알 수 없는 메시지 타입: '{msg_type}'")
            return make_error(ErrorCode.UNKNOWN_MESSAGE, f"unknown_message_type: {msg_type}")

        return await handler(msg, session_id)

    # ── DB 저장 재시도 워커 ────────────────────────────────────

    async def _save_retry_worker(self):
        """
        LearningHandler 가 DB 저장에 실패하면 _save_retry_queue 에 넣는다.
        이 워커가 5초 대기 후 한 번 더 저장을 시도한다.
        최종 실패 시 ERROR 로그만 남기고 버린다.
        """
        logger.info("[DB] 저장 재시도 워커 시작")
        while True:
            kwargs = await self._save_retry_queue.get()
            await asyncio.sleep(5)  # 잠시 대기 후 재시도 (DB 일시 장애 대비)
            try:
                await self._learning._save_result(**kwargs)
                logger.info(
                    f"[DB] 재시도 성공 — "
                    f"user={kwargs.get('user_id')} word={kwargs.get('word_id')}"
                )
            except Exception as e:
                logger.error(
                    f"[DB] 재시도 최종 실패 — "
                    f"user={kwargs.get('user_id')} word={kwargs.get('word_id')}: "
                    f"{type(e).__name__}: {e}",
                    exc_info=True
                )

# ──────────────────────────────────────────────────────────────
# 엔트리포인트
# ──────────────────────────────────────────────────────────────

async def main():
    config = ServerConfig()
    server = OpsServer(config)
    loop   = asyncio.get_event_loop()
    try:
        for sig in (signal.SIGINT, signal.SIGTERM):
            loop.add_signal_handler(sig, server.shutdown)
    except NotImplementedError:
        # Windows 는 add_signal_handler 미지원 → Ctrl+C 로 종료
        logger.warning("[Server] signal.add_signal_handler 미지원 환경 (Windows)")
    await server.start()



if __name__ == "__main__":
    asyncio.run(main())
