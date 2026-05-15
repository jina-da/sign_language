from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from routers import video
from dotenv import load_dotenv
import os

load_dotenv()

app = FastAPI(
    title="Video Streaming Server",
    description="세션 인증 기반 동영상 스트리밍 서버",
    version="1.0.0",
)

# CORS: 운영 서버 도메인만 허용 (쿠키 전달을 위해 allow_credentials=True 필수)
ALLOWED_ORIGINS = os.getenv("ALLOWED_ORIGINS", "http://localhost:3000").split(",")

app.add_middleware(
    CORSMiddleware,
    allow_origins=ALLOWED_ORIGINS,
    allow_credentials=False,   # 쿠키 미사용 (session_token을 쿼리 파라미터로 전달)
    allow_methods=["GET"],
    allow_headers=["*"],
)

app.include_router(video.router)


@app.get("/health", tags=["Health"])
async def health_check():
    return {"status": "ok"}
