# ================================================================
# 운영 서버에 추가할 코드 (참고용)
#
# 클라이언트가 동영상 요청 시 FastAPI가 이 엔드포인트를 호출합니다.
# 기존 운영 서버의 세션 저장 방식에 맞게 조회 로직만 수정하세요.
# ================================================================


# ── FastAPI 운영 서버인 경우 ──────────────────────────────────────────
from fastapi import FastAPI, HTTPException
from pydantic import BaseModel
import time

app = FastAPI()

class SessionVerifyRequest(BaseModel):
    session_id: str


@app.post("/api/verify-session")
def verify_session(body: SessionVerifyRequest):
    """
    FastAPI 동영상 서버로부터 세션 유효성 검증 요청을 받습니다.
    유효한 세션이면 200 OK, 아니면 401을 반환합니다.

    ※ 아래 active_sessions를 실제 세션 저장소(DB / Redis)로 교체하세요.
    """

    # ▼ 실제 운영 코드로 교체할 부분 ▼
    # 예시 1) DB 조회:
    #   session = db.query(Session).filter_by(id=body.session_id).first()
    #
    # 예시 2) Redis 조회:
    #   session = redis_client.hgetall(f"session:{body.session_id}")
    session = active_sessions.get(body.session_id)  # 예시용 인메모리
    # ▲ 실제 운영 코드로 교체할 부분 ▲

    if not session:
        raise HTTPException(status_code=401, detail="존재하지 않는 세션")

    if time.time() > session.get("expires_at", 0):
        del active_sessions[body.session_id]
        raise HTTPException(status_code=401, detail="세션 만료")

    return {"valid": True, "user_id": session["user_id"]}


# 예시용 인메모리 세션 저장소 (실제 운영에서는 DB/Redis로 교체)
active_sessions: dict[str, dict] = {
    "sample_session_abc123": {
        "user_id": "user_001",
        "expires_at": time.time() + 3600,  # 1시간
    }
}


# ── Flask 운영 서버인 경우 ────────────────────────────────────────────
# from flask import Flask, request, jsonify
#
# @app.route("/api/verify-session", methods=["POST"])
# def verify_session():
#     data       = request.get_json()
#     session_id = data.get("session_id")
#
#     # 실제 세션 저장소(DB / Redis)에서 조회
#     session = active_sessions.get(session_id)
#     if not session:
#         return jsonify({"error": "Invalid session"}), 401
#
#     return jsonify({"valid": True}), 200
