"""
SignLearn DB 관리자 - 디버그 로깅 강화본
변경 내용:
  ✅ logger.py 중앙 로거 사용
  ✅ 모든 쿼리 실행 전 DEBUG 로그 (SQL + 파라미터)
  ✅ 풀 획득 타임아웃 → ERROR 로그 + 현재 풀 상태
  ✅ 캐시 갱신 시 DEBUG 로그
  ✅ 각 DB 메서드 소요시간 측정 로그
"""
import os  # 추가
import asyncio
import json
import random
import time
from contextlib import asynccontextmanager
from typing import Optional

import aiomysql

from config import ServerConfig
from logger import get_logger

logger = get_logger("db_manager")

WORD_CACHE_TTL = 3600  # 1시간
VIDEO_SERVER = os.getenv("VIDEO_SERVER_URL", "http://localhost:8000")

class DBManager:
    def __init__(self, config: ServerConfig):
        self.config = config
        self._pool: Optional[aiomysql.Pool] = None
        self._word_cache: dict = {}
        self._word_cache_ts: float = 0

    async def init_pool(self):
        logger.info(
            f"[DBPool] 연결 풀 초기화: {self.config.DB_HOST}:{self.config.DB_PORT} "
            f"db={self.config.DB_NAME} user={self.config.DB_USER} "
            f"min={self.config.DB_POOL_MIN} max={self.config.DB_POOL_MAX}"
        )
        try:
            self._pool = await aiomysql.create_pool(
                host=self.config.DB_HOST,
                port=self.config.DB_PORT,
                user=self.config.DB_USER,
                password=self.config.DB_PASSWORD,
                db=self.config.DB_NAME,
                minsize=self.config.DB_POOL_MIN,
                maxsize=self.config.DB_POOL_MAX,
                connect_timeout=self.config.DB_CONNECT_TIMEOUT,
                autocommit=False,
                charset="utf8mb4",
            )
            logger.info("[DBPool] 연결 풀 초기화 완료")
        except Exception as e:
            logger.critical(f"[DBPool] 연결 풀 초기화 실패: {type(e).__name__}: {e}", exc_info=True)
            raise

    async def close_pool(self):
        if self._pool:
            logger.info("[DBPool] 연결 풀 종료")
            self._pool.close()
            await self._pool.wait_closed()
            logger.info("[DBPool] 연결 풀 종료 완료")

    @asynccontextmanager
    async def acquire(self, timeout: float = 5.0):
        if not self._pool:
            logger.critical("[DBPool] acquire() 호출됐으나 풀 미초기화 — init_pool() 먼저 호출 필요")
            raise RuntimeError("DB 풀 미초기화")

        t0 = time.monotonic()
        try:
            async with asyncio.timeout(timeout):
                async with self._pool.acquire() as conn:
                    wait_ms = int((time.monotonic() - t0) * 1000)
                    if wait_ms > 1000:
                        logger.warning(f"[DBPool] 연결 획득 지연: {wait_ms}ms (풀 고갈 의심)")
                    else:
                        logger.debug(f"[DBPool] 연결 획득: {wait_ms}ms")
                    yield conn
        except asyncio.TimeoutError:
            pool_info = f"freesize={self._pool.freesize}" if self._pool else "pool=None"
            logger.error(
                f"[DBPool] 연결 획득 타임아웃 ({timeout}s) — {pool_info} "
                f"→ DB 풀 고갈 또는 MariaDB 응답 없음"
            )
            raise RuntimeError(f"DB 풀 획득 타임아웃 ({timeout}s)")

    # ── 사용자 ──────────────────────────────────────────────────

    async def get_user_by_credentials(self, username: str, password_hash: str) -> Optional[dict]:
        logger.debug(f"[DB] get_user_by_credentials: username='{username}'")
        t0 = time.monotonic()
        async with self.acquire() as conn:
            async with conn.cursor(aiomysql.DictCursor) as cur:
                await cur.execute(
                    "SELECT id, username, password_hash, is_dominant_left, is_active, "
                    "is_deaf, keypoint_consent, daily_goal "
                    "FROM users "
                    "WHERE username=%s AND is_active=1",
                    (username,),
                )
                row = await cur.fetchone()
        elapsed = int((time.monotonic() - t0) * 1000)
        if row:
            logger.debug(f"[DB] DB hash 앞 8자리: '{row['password_hash'][:8]}'")
            logger.debug(f"[DB] 클라이언트 hash 앞 8자리: '{password_hash[:8]}'")
            if row["password_hash"] != password_hash:
                logger.warning(f"[DB] 비밀번호 불일치: username='{username}' ({elapsed}ms)")
                return None
            logger.debug(f"[DB] get_user_by_credentials 성공: user_id={row['id']} ({elapsed}ms)")
        else:
            logger.debug(f"[DB] get_user_by_credentials 없음: username='{username}' ({elapsed}ms)")
        return row
    async def create_user(self, username, password_hash, is_deaf, is_dominant_left, keypoint_consent) -> int:
        logger.debug(
            f"[DB] create_user: username='{username}' is_deaf={is_deaf} "
            f"dominant_left={is_dominant_left} consent={keypoint_consent}"
        )
        t0 = time.monotonic()
        async with self.acquire() as conn:
            async with conn.cursor() as cur:
                try:
                    await cur.execute(
                        "INSERT INTO users "
                        "(username, password_hash, is_deaf, is_dominant_left, keypoint_consent) "
                        "VALUES (%s, %s, %s, %s, %s)",
                        (username, password_hash, is_deaf, is_dominant_left, keypoint_consent),
                    )
                    await conn.commit()
                    new_id = cur.lastrowid
                except Exception as e:
                    logger.error(
                        f"[DB] create_user 실패: username='{username}': "
                        f"{type(e).__name__}: {e}",
                        exc_info=True
                    )
                    raise
        elapsed = int((time.monotonic() - t0) * 1000)
        logger.info(f"[DB] create_user 성공: user_id={new_id} username='{username}' ({elapsed}ms)")
        return new_id

    async def get_user_consent(self, user_id: int, conn) -> bool:
        async with conn.cursor() as cur:
            await cur.execute("SELECT keypoint_consent FROM users WHERE id=%s", (user_id,))
            row = await cur.fetchone()
            result = bool(row and row[0])
        logger.debug(f"[DB] get_user_consent: user={user_id} → {result}")
        return result

    # ── 세션 ────────────────────────────────────────────────────

    async def upsert_session(self, session_id: str, user_id: int):
        logger.debug(f"[DB] upsert_session: user={user_id} sid={session_id[:8]}")
        async with self.acquire() as conn:
            async with conn.cursor() as cur:
                await cur.execute(
                    "INSERT INTO user_session (session_id, user_id, started_at, last_active) "
                    "VALUES (%s, %s, NOW(), NOW()) "
                    "ON DUPLICATE KEY UPDATE last_active=NOW()",
                    (session_id, user_id),
                )
                await conn.commit()

    async def close_session(self, session_id: str, word_count: int, duration_sec: int):
        logger.debug(f"[DB] close_session: sid={session_id[:8]} words={word_count} dur={duration_sec}s")
        try:
            async with self.acquire(timeout=3.0) as conn:
                async with conn.cursor() as cur:
                    await conn.begin()  # ← 추가
                    await cur.execute(
                        "UPDATE user_session SET ended_at=NOW(), word_count=%s, duration_sec=%s "
                        "WHERE session_id=%s",
                        (word_count, duration_sec, session_id),
                    )
                    await conn.commit()
        except Exception as e:
            logger.error(f"[DB] close_session 실패 sid={session_id[:8]}: {type(e).__name__}: {e}")

    # ── 단어 / 진도 ──────────────────────────────────────────────

    async def _refresh_word_cache(self):
        logger.info("[DB] word_info 캐시 갱신 시작")
        t0 = time.monotonic()
        async with self.acquire() as conn:
            async with conn.cursor(aiomysql.DictCursor) as cur:
                await cur.execute("SELECT id, word, meaning, difficulty, video_path, word_number FROM word_info")
                rows = await cur.fetchall()
        self._word_cache = {r["id"]: r for r in rows}
        self._word_cache_ts = time.time()
        elapsed = int((time.monotonic() - t0) * 1000)
        logger.info(f"[DB] word_info 캐시 갱신 완료: {len(self._word_cache)}개 ({elapsed}ms)")

    async def get_word_list(self, user_id: int, mode: str) -> list:
        logger.debug(f"[DB] get_word_list: user={user_id} mode={mode}")
        t0 = time.monotonic()

        if time.time() - self._word_cache_ts > WORD_CACHE_TTL:
            logger.debug("[DB] 단어 캐시 만료 — 갱신")
            await self._refresh_word_cache()

        async with self.acquire() as conn:

            await conn.commit()

            async with conn.cursor(aiomysql.DictCursor) as cur:
                # 1. 사용자의 daily_goal 조회
                await cur.execute("SELECT daily_goal FROM users WHERE id = %s", (user_id,))
                user_row = await cur.fetchone()
                # 사용자 정보가 없거나 설정이 없으면 기본값 10 사용
                limit_count = user_row['daily_goal'] if user_row else 10

                if mode == "review":
                    await cur.execute(
                        """
                        SELECT w.id, w.word, w.meaning, up.accuracy, up.attempts, w.video_path
                        FROM word_info w
                        JOIN user_progress up ON w.id = up.word_id
                        WHERE up.user_id = %s AND (up.accuracy < 0.8 OR up.attempts < 3)
                        ORDER BY accuracy ASC
                        LIMIT %s
                        """,
                        (user_id, limit_count),
                    )
                    rows = await cur.fetchall()

                    # video_cdn_url 변환 추가 ↓
                    for word in rows:
                        filename = os.path.basename(word["video_path"]) if word.get("video_path") else None
                        word["video_cdn_url"] = f"{VIDEO_SERVER}/video/{filename}" if filename else None

                elif mode == "study":

                    logger.debug(f"[Check] 쿼리 실행 직전 limit_count: {limit_count}")

                    await cur.execute(
                        """
                        SELECT w.id, w.word, w.meaning, w.difficulty, w.video_path
                        FROM word_info w
                        LEFT JOIN user_progress up ON w.id = up.word_id AND up.user_id = %s
                        WHERE up.word_id IS NULL
                        ORDER BY w.difficulty ASC
                        LIMIT %s
                        """,
                        (user_id,limit_count),
                    )
                    rows = await cur.fetchall()

                        # ↓ 이 4줄 추가
                    for row in rows:
                        filename = os.path.basename(row["video_path"]) if row.get("video_path") else None
                        row["video_cdn_url"] = f"{VIDEO_SERVER}/video/{filename}" if filename else None
                        del row["video_path"]

                else:  # game
                    await cur.execute(
                        "SELECT w.id, w.word, w.meaning "
                        "FROM word_info w "
                        "JOIN user_progress up ON w.id = up.word_id "
                        "WHERE up.user_id = %s",
                        (user_id,),
                    )
                    all_rows = await cur.fetchall()
                    rows = random.sample(all_rows, min(30, len(all_rows)))

        elapsed = int((time.monotonic() - t0) * 1000)
        logger.debug(f"[DB] get_word_list 완료: user={user_id} mode={mode} → {len(rows)}개 ({elapsed}ms)")
        return rows

    async def get_user_progress(self, user_id: int) -> list:
        logger.debug(f"[DB] get_user_progress: user={user_id}")
        t0 = time.monotonic()
        async with self.acquire() as conn:
            async with conn.cursor(aiomysql.DictCursor) as cur:
                await cur.execute(
                    """
                    SELECT w.word, w.meaning, up.accuracy, up.attempts, up.last_practiced
                    FROM user_progress up
                    JOIN word_info w ON up.word_id = w.id
                    WHERE up.user_id = %s
                    ORDER BY up.last_practiced DESC
                    LIMIT 100
                    """,
                    (user_id,),
                )
                rows = await cur.fetchall()
        elapsed = int((time.monotonic() - t0) * 1000)
        logger.debug(f"[DB] get_user_progress 완료: user={user_id} → {len(rows)}개 ({elapsed}ms)")
        return rows

    # ── 통계 ────────────────────────────────────────────────────

    async def update_word_stats(self, word_id: int, confidence: float):
        logger.debug(f"[DB] update_word_stats: word={word_id} confidence={confidence:.4f}")
        try:
            async with self.acquire(timeout=3.0) as conn:
                async with conn.cursor() as cur:
                    await cur.execute(
                        """
                        INSERT INTO word_stats (word_id, avg_accuracy, total_attempts)
                        VALUES (%s, %s, 1)
                        ON DUPLICATE KEY UPDATE
                            avg_accuracy = (avg_accuracy * total_attempts + VALUES(avg_accuracy)) / (total_attempts + 1),
                            total_attempts = total_attempts + 1
                        """,
                        (word_id, confidence),
                    )
                    await conn.commit()
        except Exception as e:
            logger.warning(f"[DB] update_word_stats 실패 word={word_id}: {type(e).__name__}: {e}")

    # ── 게임 ────────────────────────────────────────────────────

    async def save_game_history(self, user_id, mode, score, duration_sec, word_results):
        logger.debug(f"[DB] save_game_history: user={user_id} mode={mode} score={score}")
        t0 = time.monotonic()
        try:
            async with self.acquire() as conn:
                async with conn.cursor() as cur:
                    await cur.execute(
                        "INSERT INTO game_history "
                        "(user_id, mode, score, duration_sec, word_results_json, played_at) "
                        "VALUES (%s, %s, %s, %s, %s, NOW())",
                        (user_id, mode, score, duration_sec, json.dumps(word_results)),
                    )
                    await conn.commit()
        except Exception as e:
            logger.error(
                f"[DB] save_game_history 실패: user={user_id} mode={mode}: "
                f"{type(e).__name__}: {e}",
                exc_info=True
            )
            raise
        elapsed = int((time.monotonic() - t0) * 1000)
        logger.debug(f"[DB] save_game_history 완료 user={user_id} ({elapsed}ms)")

    # ── 재학습 ──────────────────────────────────────────────────

    async def count_untrained_keypoints(self) -> int:
        async with self.acquire() as conn:
            async with conn.cursor() as cur:
                await cur.execute("SELECT COUNT(*) FROM keypoint_store WHERE is_trained=0")
                row = await cur.fetchone()
                count = row[0] if row else 0
        logger.debug(f"[DB] count_untrained_keypoints: {count}개")
        return count

    async def get_untrained_keypoints(self) -> list:
        logger.debug("[DB] get_untrained_keypoints 시작")
        async with self.acquire() as conn:
            async with conn.cursor(aiomysql.DictCursor) as cur:
                await cur.execute(
                    "SELECT id, word_id, keypoint_data, confidence FROM keypoint_store "
                    "WHERE is_trained=0 ORDER BY created_at ASC LIMIT 5000"
                )
                rows = await cur.fetchall()
        logger.debug(f"[DB] get_untrained_keypoints: {len(rows)}개 로드")
        return rows

    async def mark_keypoints_trained(self, ids: list):
        if not ids:
            logger.debug("[DB] mark_keypoints_trained: 빈 목록, 생략")
            return
        logger.info(f"[DB] mark_keypoints_trained: {len(ids)}개 is_trained=1 업데이트")
        placeholders = ",".join(["%s"] * len(ids))
        async with self.acquire() as conn:
            async with conn.cursor() as cur:
                await cur.execute(
                    f"UPDATE keypoint_store SET is_trained=1 WHERE id IN ({placeholders})", ids
                )
                await conn.commit()

    async def purge_old_keypoints(self, days: int = 90):
        logger.info(f"[DB] purge_old_keypoints: {days}일 이상 미학습 데이터 삭제 시작")
        async with self.acquire() as conn:
            async with conn.cursor() as cur:
                await cur.execute(
                    "DELETE FROM keypoint_store "
                    "WHERE is_trained=0 AND created_at < DATE_SUB(NOW(), INTERVAL %s DAY)",
                    (days,),
                )
                deleted = cur.rowcount
                await conn.commit()
        logger.info(f"[DB] purge_old_keypoints: {deleted}행 삭제 완료")

    async def get_active_model_version(self) -> Optional[dict]:
        async with self.acquire() as conn:
            async with conn.cursor(aiomysql.DictCursor) as cur:
                await cur.execute(
                    "SELECT * FROM model_versions WHERE is_active=1 ORDER BY id DESC LIMIT 1"
                )
                row = await cur.fetchone()
        if row:
            logger.debug(f"[DB] 활성 모델: version={row.get('version')} accuracy={row.get('accuracy')}")
        else:
            logger.warning("[DB] 활성 모델 없음 (model_versions 테이블 비어있음)")
        return row

    async def add_model_version(self, version, accuracy, file_path, is_active) -> int:
        logger.info(
            f"[DB] add_model_version: version={version} accuracy={accuracy:.4f} "
            f"file={file_path} is_active={is_active}"
        )
        async with self.acquire() as conn:
            async with conn.cursor() as cur:
                if is_active:
                    await cur.execute("UPDATE model_versions SET is_active=0")
                    logger.debug("[DB] 기존 활성 모델 비활성화")
                await cur.execute(
                    "INSERT INTO model_versions (version, accuracy, file_path, is_active, trained_at) "
                    "VALUES (%s, %s, %s, %s, NOW())",
                    (version, accuracy, file_path, is_active),
                )
                await conn.commit()
                new_id = cur.lastrowid
        logger.info(f"[DB] add_model_version 완료: id={new_id} version={version}")
        return new_id

    # ── 신규 메서드 (명세서 기반 추가) ──────────────────────────

    async def get_active_model_version_id(self) -> int:
        """
        현재 활성 모델의 version id 반환.
        NO.102 RES_LOGIN, NO.501 REQ_AI_INFER, NO.702 RES_PONG 등에서 사용.
        """
        row = await self.get_active_model_version()
        if row:
            return row["id"]
        logger.warning("[DB] 활성 모델 없음 — model_version_id=0 반환")
        return 0

    async def search_word_forward(self, query: str) -> list:
        """
        NO.301 REQ_DICT_SEARCH 정방향 사전 검색 (단어 → 영상).
        word LIKE 검색으로 동음이의어 포함 최대 20개 반환.
        """
        logger.debug(f"[DB] search_word_forward: query='{query}'")
        t0 = time.monotonic()
        async with self.acquire() as conn:
            async with conn.cursor(aiomysql.DictCursor) as cur:
                like_query = f"%{query}%"
                await cur.execute(
                    "SELECT id, word, meaning, difficulty, video_path, word_number "
                    "FROM word_info "
                    "WHERE word LIKE %s "
                    "ORDER BY difficulty ASC "
                    "LIMIT 20",
                    (like_query,),
                )
                rows = await cur.fetchall()
        elapsed = int((time.monotonic() - t0) * 1000)
        logger.debug(f"[DB] search_word_forward: query='{query}' → {len(rows)}개 ({elapsed}ms)")
        return rows

    async def get_word_info(self, word_id: int) -> Optional[dict]:
        """
        NO.304 RES_DICT_REVERSE 에서 word + description 반환용.
        캐시 우선, 없으면 DB 조회.
        """
        logger.debug(f"[DB] get_word_info: word_id={word_id}")

        # 캐시 확인
        if time.time() - self._word_cache_ts <= WORD_CACHE_TTL:
            cached = self._word_cache.get(word_id)
            if cached:
                return cached

        async with self.acquire() as conn:
            async with conn.cursor(aiomysql.DictCursor) as cur:
                await cur.execute(
                    "SELECT id, word, meaning, video_cdn_url FROM word_info WHERE id = %s",
                    (word_id,),
                )
                row = await cur.fetchone()

        if row:
            logger.debug(f"[DB] get_word_info 성공: word='{row['word']}'")
        else:
            logger.warning(f"[DB] get_word_info 없음: word_id={word_id}")
        return row

    async def update_user_field(self, user_id: int, field: str, value) -> None:
        if isinstance(value, bool):
            value = int(value)  # True→1, False→0

        """
        users 테이블의 단일 컬럼을 갱신한다.
        개인설정 핸들러(NO.801~812)에서 공통으로 사용.
        """
        logger.debug(f"[DB] update_user_field: user={user_id} field={field} value={value}")
        async with self.acquire(timeout=3.0) as conn:
            try:
                # aiomysql은 기본적으로 autocommit=False일 때
                # execute만으로도 트랜잭션이 시작될 수 있으므로 commit만 확실히 해줍니다.
                async with conn.cursor() as cur:
                    await cur.execute(
                        f"UPDATE users SET {field} = %s WHERE id = %s",
                        (value, user_id),
                    )
                await conn.commit()  # 여기서 확실히 DB에 꽂아넣습니다.
            except Exception as e:
                await conn.rollback()
                logger.error(f"[DB] update_user_field 오류: {e}")
                raise

        logger.info(f"[DB] update_user_field 완료: user={user_id} {field}={value}")

    async def verify_password(self, user_id: int, password_hash: str) -> bool:
        """
        비밀번호 해시 일치 여부 확인.
        NO.807 REQ_CHANGE_PASSWORD, NO.811 REQ_WITHDRAW 에서 사용.
        """
        logger.debug(f"[DB] verify_password: user={user_id}")
        async with self.acquire(timeout=3.0) as conn:
            async with conn.cursor(aiomysql.DictCursor) as cur:
                await cur.execute(
                    "SELECT password_hash FROM users WHERE id = %s AND is_active = 1",
                    (user_id,),
                )
                row = await cur.fetchone()
        result = row is not None and row["password_hash"] == password_hash
        logger.debug(f"[DB] verify_password: user={user_id} → {'일치' if result else '불일치'}")
        return result

    async def get_review_pending_count(self, user_id: int) -> int:
        """복습 대기 단어 수 — RES_LOGIN 응답에 포함"""
        async with self.acquire(timeout=3.0) as conn:
            async with conn.cursor(aiomysql.DictCursor) as cur:
                await cur.execute(
                    """
                    SELECT COUNT(*) AS cnt FROM user_progress
                    WHERE user_id = %s
                      AND (accuracy < 0.8 OR attempts < 3)
                    """,
                    (user_id,)
                )
                row = await cur.fetchone()
                return row["cnt"] if row else 0

    async def get_high_score(self, user_id: int) -> int:
        """게임 최고 점수 — RES_LOGIN 응답에 포함 (게임 미구현 시 0 반환)"""
        async with self.acquire(timeout=3.0) as conn:
            async with conn.cursor(aiomysql.DictCursor) as cur:
                await cur.execute(
                    "SELECT COALESCE(MAX(score), 0) AS high FROM game_history WHERE user_id = %s",
                    (user_id,)
                )
                row = await cur.fetchone()
                return row["high"] if row else 0

    async def get_today_completed_count(self, user_id: int) -> int:
        """오늘 완료한 단어 수 — RES_DAILY_WORDS 응답에 포함"""
        async with self.acquire(timeout=3.0) as conn:
            async with conn.cursor(aiomysql.DictCursor) as cur:
                await cur.execute(
                    """
                    SELECT COUNT(*) AS cnt FROM user_progress
                    WHERE user_id = %s
                      AND DATE(last_practiced) = CURDATE()
                      AND accuracy >= 0.8
                    """,
                    (user_id,)
                )
                row = await cur.fetchone()
                return row["cnt"] if row else 0

    async def get_today_learned_words(self, user_id: int) -> list:
        """오늘 학습한 단어 목록 — RES_DAILY_WORDS 응답에 포함"""
        async with self.acquire(timeout=3.0) as conn:
            async with conn.cursor(aiomysql.DictCursor) as cur:
                await cur.execute(
                    """
                    SELECT wi.id AS word_id, wi.word, wi.meaning
                    FROM user_progress up
                    JOIN word_info wi ON up.word_id = wi.id
                    WHERE up.user_id = %s
                      AND DATE(up.last_practiced) = CURDATE()
                      AND up.accuracy >= 0.8
                    ORDER BY up.last_practiced DESC
                    """,
                    (user_id,)
                )
                rows = await cur.fetchall()
                return [
                    {
                        "word_id": row["word_id"],
                        "word": row["word"],
                        "meaning": row["meaning"],
                    }
                    for row in rows
                ]


