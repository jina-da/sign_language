# FastAPI 동영상 스트리밍 서버

## 서버 구성 (같은 컴퓨터)

```
[같은 서버 컴퓨터]
├── 운영 서버         → 포트 8001  (로그인 / 세션 관리)
├── FastAPI 동영상    → 포트 8000  (세션 검증 + 스트리밍)
├── mp4 파일들        → /실제/동영상/경로/
└── Nginx             → 포트 443   (리버스 프록시)
```

## 빠른 시작

```bash
# 1. 의존성 설치
pip install -r requirements.txt

# 2. .env 수정 (필수)
VIDEO_DIR=/실제/동영상/경로        # mp4 파일 실제 경로
PROD_SERVER_URL=http://localhost:8001  # 운영 서버 포트
SESSION_COOKIE_NAME=session_id         # 운영 서버 쿠키 이름과 일치

# 3. 서버 실행
# 개발
uvicorn main:app --reload --host 0.0.0.0 --port 8000

# 운영
gunicorn main:app -w 4 -k uvicorn.workers.UvicornWorker --bind 0.0.0.0:8000
```

## 운영 서버에 추가할 것

`production_server_additions.py` 참고하여
기존 운영 서버에 `/api/verify-session` 엔드포인트 추가.

## API

| 메서드 | 경로 | 설명 |
|--------|------|------|
| GET | `/video/{filename}` | 세션 검증 후 동영상 스트리밍 |
| GET | `/health` | 서버 상태 확인 |

## 인증 흐름

```
클라이언트
  → GET /video/NIA_SL_WORD0001_SYN01_F.mp4 (쿠키 자동 포함)
  → FastAPI: 쿠키에서 session_id 추출
  → 운영 서버 POST /api/verify-session 호출
  → 검증 성공 → mp4 스트리밍
```
