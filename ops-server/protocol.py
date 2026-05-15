"""
SignLearn 통신 프로토콜
명세서 NO.101~703 기준으로 MessageType 전면 재정의.

변경 내용:
  ✅ MessageType 상수를 명세서 명칭(REQ_*/RES_*/NOTIFY_*) 체계로 전면 교체
  ✅ 명세 누락 타입 전부 추가 (NOTIFY_RETRAIN_DONE, REQ_PING, RES_PONG 등)
  ✅ make_error() 헬퍼 및 ErrorCode 상수 추가
  ✅ REQ_PING body=0 수신 시 자동 처리
  ✅ logger.py 중앙 로거 사용
  ✅ 페이로드 크기 초과 / 수신 오류 상세 로그
"""

import asyncio
import json
import struct

from logger import get_logger

logger = get_logger("protocol")

MAX_PAYLOAD_SIZE = 10 * 1024 * 1024  # 10 MB
HEADER_SIZE = 4


class MessageType:
    """
    명세서 메시지 타입 정의 (NO.101 ~ 703)
    방향 표기: C=클라이언트, S=운용서버, A=AI서버
    """

    # ── 인증 (NO.101~106) ──────────────────────────────────────
    REQ_LOGIN       = "REQ_LOGIN"       # 101  C→S  로그인 요청
    RES_LOGIN       = "RES_LOGIN"       # 102  S→C  로그인 응답
    REQ_LOGOUT      = "REQ_LOGOUT"      # 103  C→S  로그아웃 요청 (명세 103 명칭 오타 수정)
    RES_LOGOUT      = "RES_LOGOUT"      # 104  S→C  로그아웃 응답
    REQ_REGISTER    = "REQ_REGISTER"    # 105  C→S  회원가입 요청
    RES_REGISTER    = "RES_REGISTER"    # 106  S→C  회원가입 응답

    # ── 학습 / 추론 (NO.201~206) ───────────────────────────────
    REQ_DAILY_WORDS  = "REQ_DAILY_WORDS"   # 201  C→S  오늘의 단어 요청
    RES_DAILY_WORDS  = "RES_DAILY_WORDS"   # 202  S→C  오늘의 단어 응답
    REQ_INFER        = "REQ_INFER"         # 203  C→S  수화 추론 요청
    RES_INFER        = "RES_INFER"         # 204  S→C  추론 결과 응답
    REQ_REVIEW_WORDS = "REQ_REVIEW_WORDS"  # 205  C→S  복습 단어 요청
    RES_REVIEW_WORDS = "RES_REVIEW_WORDS"  # 206  S→C  복습 단어 응답

    # ── 사전 (NO.301~304) ──────────────────────────────────────
    REQ_DICT_SEARCH  = "REQ_DICT_SEARCH"   # 301  C→S  정방향 사전 검색 요청
    RES_DICT_SEARCH  = "RES_DICT_SEARCH"   # 302  S→C  정방향 사전 검색 응답
    REQ_DICT_REVERSE = "REQ_DICT_REVERSE"  # 303  C→S  역방향 사전 추론 요청
    RES_DICT_REVERSE = "RES_DICT_REVERSE"  # 304  S→C  역방향 사전 결과 응답

    # ── 게임 (NO.401~406) ──────────────────────────────────────
    REQ_GAME_START = "REQ_GAME_START"  # 401  C→S  게임 시작 요청
    RES_GAME_START = "RES_GAME_START"  # 402  S→C  게임 시작 응답
    REQ_GAME_INFER = "REQ_GAME_INFER"  # 403  C→S  게임 추론 요청
    RES_GAME_INFER = "RES_GAME_INFER"  # 404  S→C  게임 추론 응답
    REQ_GAME_END   = "REQ_GAME_END"    # 405  C→S  게임 종료 요청
    RES_GAME_END   = "RES_GAME_END"    # 406  S→C  게임 종료 응답

    # ── AI 추론 (NO.501~502) ──────────────────────────────────
    REQ_AI_INFER = "REQ_AI_INFER"  # 501  S→A  AI 추론 요청
    RES_AI_INFER = "RES_AI_INFER"  # 502  A→S  AI 추론 응답

    # ── 재학습 (NO.601~607) ───────────────────────────────────
    REQ_RETRAIN         = "REQ_RETRAIN"         # 601  S→A  재학습 시작 명령
    RES_RETRAIN_ACK     = "RES_RETRAIN_ACK"     # 602  A→S  재학습 수신 확인
    NOTIFY_RETRAIN_DONE = "NOTIFY_RETRAIN_DONE" # 603  A→S  재학습 완료 알림
    REQ_MODEL_DEPLOY    = "REQ_MODEL_DEPLOY"    # 604  S→A  모델 배포 승인
    RES_MODEL_DEPLOY    = "RES_MODEL_DEPLOY"    # 605  A→S  모델 배포 완료
    REQ_MODEL_ROLLBACK  = "REQ_MODEL_ROLLBACK"  # 606  S→A  모델 롤백 명령
    RES_MODEL_ROLLBACK  = "RES_MODEL_ROLLBACK"  # 607  A→S  롤백 완료 응답

    # ── 헬스체크 / 공통 (NO.701~703) ─────────────────────────
    REQ_PING  = "REQ_PING"   # 701  S→A  AI 서버 생존 확인 (body 없음)
    RES_PONG  = "RES_PONG"   # 702  A→S  생존 응답
    RES_ERROR = "RES_ERROR"  # 703  S→C / A→S  범용 오류 응답

    # ── 개인설정 /(NO.803~812) ─────────────────────────
    # ── 개인설정 (NO.801~812) ──────────────────────────────────
    REQ_SET_DAILY_GOAL = "REQ_SET_DAILY_GOAL"  # 801  C→S  하루 목표 단어 수 변경
    RES_SET_DAILY_GOAL = "RES_SET_DAILY_GOAL"  # 802  S→C  하루 목표 단어 수 변경 응답
    REQ_SET_DOMINANT_HAND = "REQ_SET_DOMINANT_HAND"  # 803  C→S  우세손 변경
    RES_SET_DOMINANT_HAND = "RES_SET_DOMINANT_HAND"  # 804  S→C  우세손 변경 응답
    REQ_SET_DEAF = "REQ_SET_DEAF"  # 805  C→S  농인 여부 변경
    RES_SET_DEAF = "RES_SET_DEAF"  # 806  S→C  농인 여부 변경 응답
    REQ_CHANGE_PASSWORD = "REQ_CHANGE_PASSWORD"  # 807  C→S  비밀번호 변경
    RES_CHANGE_PASSWORD = "RES_CHANGE_PASSWORD"  # 808  S→C  비밀번호 변경 응답
    REQ_SET_CONSENT = "REQ_SET_CONSENT"  # 809  C→S  키포인트 수집 동의 변경
    RES_SET_CONSENT = "RES_SET_CONSENT"  # 810  S→C  키포인트 수집 동의 변경 응답
    REQ_WITHDRAW = "REQ_WITHDRAW"  # 811  C→S  회원 탈퇴
    RES_WITHDRAW = "RES_WITHDRAW"  # 812  S→C  회원 탈퇴 응답

def make_error(error_code: int, message: str) -> dict:
    """NO.703 RES_ERROR 공통 오류 응답 생성 헬퍼"""
    return {
        "type": MessageType.RES_ERROR,
        "error_code": error_code,
        "message": message,
    }

class ErrorCode:
    """NO.703 RES_ERROR 에서 사용하는 error_code 상수"""
    # 인증
    INVALID_CREDENTIALS  = 1001
    SESSION_EXPIRED      = 1002
    NOT_AUTHENTICATED    = 1003
    USERNAME_TAKEN       = 1004

    # 추론 유효성
    INVALID_KEYPOINT     = 2001
    INVALID_FRAME_COUNT  = 2002
    INVALID_KEYPOINT_DIM = 2003

    # AI 서버
    AI_TIMEOUT           = 3001
    AI_POOL_EXHAUSTED    = 3002
    AI_INFER_ERROR       = 3003
    INVALID_AI_RESPONSE  = 3004

    # 사전
    WORD_NOT_FOUND       = 4001

    # 게임
    GAME_NOT_FOUND       = 5001
    GAME_ALREADY_ENDED   = 5002

    # 서버
    SERVER_ERROR         = 9001
    UNKNOWN_MESSAGE      = 9002


class Protocol:
    @staticmethod
    async def send_message(writer: asyncio.StreamWriter, data: dict) -> None:
        try:
            payload = json.dumps(data, ensure_ascii=False).encode("utf-8")
        except (TypeError, ValueError) as e:
            logger.error(f"[Protocol] JSON 직렬화 실패: {type(e).__name__}: {e} | data keys={list(data.keys())}")
            raise

        if len(payload) > MAX_PAYLOAD_SIZE:
            logger.error(
                f"[Protocol] send_message 크기 초과: {len(payload)} bytes "
                f"(최대={MAX_PAYLOAD_SIZE}) — type={data.get('type')}"
            )
            raise ValueError(f"페이로드 너무 큼: {len(payload)} bytes")

        header = struct.pack(">I", len(payload))
        logger.debug(f"[Protocol] send: type={data.get('type', '?')} size={len(payload)}bytes")

        try:
            writer.write(header + payload)
            await writer.drain()
        except Exception as e:
            logger.error(
                f"[Protocol] 전송 실패: {type(e).__name__}: {e} "
                f"type={data.get('type')}",
                exc_info=True
            )
            raise

    @staticmethod
    async def recv_message(reader: asyncio.StreamReader) -> dict | None:
        try:
            header = await reader.readexactly(HEADER_SIZE)
        except asyncio.IncompleteReadError as e:
            if len(e.partial) == 0:
                logger.debug("[Protocol] recv: 연결 정상 종료 (EOF)")
            else:
                logger.warning(
                    f"[Protocol] recv: 헤더 불완전 수신 — "
                    f"수신={len(e.partial)}bytes (기대={HEADER_SIZE}bytes)"
                )
            return None
        except Exception as e:
            logger.error(f"[Protocol] recv 헤더 읽기 오류: {type(e).__name__}: {e}", exc_info=True)
            return None

        payload_size = struct.unpack(">I", header)[0]

        # NO.701 REQ_PING: body 없음 (bodySize=0)
        if payload_size == 0:
            logger.debug("[Protocol] recv: 빈 메시지 (size=0) — REQ_PING으로 처리")
            return {"type": MessageType.REQ_PING}

        if payload_size > MAX_PAYLOAD_SIZE:
            logger.error(
                f"[Protocol] recv: 페이로드 크기 초과 — "
                f"{payload_size} bytes (최대={MAX_PAYLOAD_SIZE}) "
                f"→ 비정상 클라이언트 의심"
            )
            raise ValueError(f"페이로드 크기 초과: {payload_size} bytes")

        try:
            body = await reader.readexactly(payload_size)
        except asyncio.IncompleteReadError as e:
            logger.error(
                f"[Protocol] recv: body 불완전 수신 — "
                f"수신={len(e.partial)}/{payload_size} bytes"
            )
            return None

        try:
            data = json.loads(body.decode("utf-8"))
            logger.debug(f"[Protocol] recv: type={data.get('type', '?')} size={payload_size}bytes")
            return data
        except (json.JSONDecodeError, UnicodeDecodeError) as e:
            logger.error(
                f"[Protocol] recv: JSON 파싱 실패 — {type(e).__name__}: {e} "
                f"(size={payload_size}, 앞부분={body[:100]!r})"
            )
            return None
