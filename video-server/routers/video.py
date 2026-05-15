from fastapi import APIRouter, HTTPException, Request, Query
from fastapi.responses import StreamingResponse
import aiomysql
import os
import logging

logger = logging.getLogger(__name__)

router = APIRouter(tags=["Video"])

# ── 환경 변수 ──────────────────────────────────────────────────────────
VIDEO_DIR = os.getenv("VIDEO_DIR", "static/videos")

DB_HOST     = os.getenv("DB_HOST", "127.0.0.1")
DB_PORT     = int(os.getenv("DB_PORT", "3306"))
DB_USER     = os.getenv("DB_USER", "root")
DB_PASSWORD = os.getenv("DB_PASSWORD", "1234")
DB_NAME     = os.getenv("DB_NAME", "ksl_learning")

CHUNK_SIZE = 1024 * 1024  # 1MB 청크 단위 스트리밍

def iter_file_chunks(path: str, start: int, end: int):
    """파일을 청크 단위로 읽어 yield (메모리 효율)"""
    with open(path, "rb") as f:
        f.seek(start)
        remaining = end - start + 1
        while remaining > 0:
            chunk = f.read(min(CHUNK_SIZE, remaining))
            if not chunk:
                break
            remaining -= len(chunk)
            yield chunk

async def verify_session(session_id: str) -> bool:
    """
    MariaDB의 user_session 테이블에서 세션 유효성 직접 조회.
    ended_at IS NULL → 현재 활성 세션
    """
    try:
        conn = await aiomysql.connect(
            host     = DB_HOST,
            port     = DB_PORT,
            user     = DB_USER,
            password = DB_PASSWORD,
            db       = DB_NAME,
        )
        async with conn.cursor() as cur:
            await cur.execute(
                """
                SELECT session_id
                FROM user_session
                WHERE session_id = %s
                  AND ended_at IS NULL
                LIMIT 1
                """,
                (session_id,)
            )
            row = await cur.fetchone()
        conn.close()
        return row is not None

    except Exception as e:
        logger.error(f"세션 DB 조회 실패: {e}")
        raise HTTPException(status_code=503, detail="세션 검증 중 오류가 발생했습니다")

@router.get("/video/{filename}", summary="동영상 스트리밍")
async def stream_video(
    filename: str,
    request: Request,
    session_token: str = Query(..., description="로그인 시 발급받은 session_token"),
):
    """
    session_token을 MariaDB에서 직접 검증한 후 mp4 파일을 스트리밍합니다.
    Range 헤더를 지원하여 영상 탐색(seek) 및 이어보기가 가능합니다.

    - **filename**: 스트리밍할 동영상 파일명 (예: NIA_SL_WORD0001_SYN01_F.mp4)
    - **session_token**: 로그인 응답의 session_token 값
    """

    # ── Step 1. 세션 토큰 DB 검증 ──────────────────────────────────────
    is_valid = await verify_session(session_token)
    if not is_valid:
        raise HTTPException(
            status_code=401,
            detail="유효하지 않거나 만료된 세션입니다. 다시 로그인해주세요.",
        )

    # ── Step 2. 경로 탐색 공격 방지 ───────────────────────────────────
    safe_filename = os.path.basename(filename)
    video_path    = os.path.join(VIDEO_DIR, safe_filename)

    if not os.path.exists(video_path):
        raise HTTPException(status_code=404, detail=f"동영상 파일을 찾을 수 없습니다: {safe_filename}")

    # ── Step 3. 동영상 스트리밍 ───────────────────────────────────────
    file_size    = os.path.getsize(video_path)
    range_header = request.headers.get("range")

    # Range 요청 처리 (seek / 이어보기)
    if range_header:
        try:
            start, end = 0, file_size - 1
            parts = range_header.replace("bytes=", "").split("-")
            start = int(parts[0])
            if parts[1]:
                end = int(parts[1])

            if start > end or start >= file_size:
                raise HTTPException(status_code=416, detail="Range Not Satisfiable")

            end            = min(end, file_size - 1)
            content_length = end - start + 1

            headers = {
                "Content-Range":  f"bytes {start}-{end}/{file_size}",
                "Accept-Ranges":  "bytes",
                "Content-Length": str(content_length),
                "Content-Type":   "video/mp4",
            }
            return StreamingResponse(
                iter_file_chunks(video_path, start, end),
                status_code=206,
                headers=headers,
            )

        except (ValueError, IndexError):
            raise HTTPException(status_code=400, detail="잘못된 Range 헤더 형식")

    # 전체 파일 스트리밍
    headers = {
        "Accept-Ranges":  "bytes",
        "Content-Length": str(file_size),
        "Content-Type":   "video/mp4",
    }
    return StreamingResponse(
        iter_file_chunks(video_path, 0, file_size - 1),
        status_code=200,
        headers=headers,
    )