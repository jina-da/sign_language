"""
SignLearn 서버 설정 변수 우선, 없으면 기본값 사용
"""

import os


class ServerConfig:
    # 운용 서버
    HOST = os.getenv("OPS_HOST", "0.0.0.0")
    CLIENT_PORT = int(os.getenv("OPS_CLIENT_PORT", "9000"))
    AI_SERVER_PORT = int(os.getenv("OPS_AI_SERVER_PORT", "9001"))  # AI 서버로부터 모델 수신
    MAX_CLIENTS = int(os.getenv("MAX_CLIENTS", "100"))
    CLIENT_TIMEOUT = None
    # float(os.getenv("CLIENT_TIMEOUT", "60.0"))  # 초

    # AI 서버
    AI_HOST = os.getenv("AI_HOST", "10.10.10.128")
    AI_PORT = int(os.getenv("AI_PORT", "9100"))
    AI_TIMEOUT = float(os.getenv("AI_TIMEOUT", "3600.0"))  # 추론 타임아웃 (목표 1초 이내)
    AI_RECONNECT_ATTEMPTS = int(os.getenv("AI_RECONNECT_ATTEMPTS", "1")) #AI 슬롯 당 재시도 횟수

    # MariaDB
    DB_HOST = os.getenv("DB_HOST", "127.0.0.1")
    DB_PORT = int(os.getenv("DB_PORT", "3306"))
    DB_USER = os.getenv("DB_USER", "root")
    DB_PASSWORD = os.getenv("DB_PASSWORD", "1234")
    DB_NAME = os.getenv("DB_NAME", "ksl_learning")
    DB_POOL_MIN = int(os.getenv("DB_POOL_MIN", "5"))
    DB_POOL_MAX = int(os.getenv("DB_POOL_MAX", "20"))
    DB_CONNECT_TIMEOUT = int(os.getenv("DB_CONNECT_TIMEOUT", "5"))

    # 재학습
    RETRAIN_DATA_THRESHOLD = int(os.getenv("RETRAIN_DATA_THRESHOLD", "1000"))
    RETRAIN_HOUR = int(os.getenv("RETRAIN_HOUR", "3"))  # 새벽 3시

    # 판정 기준 (FixMatch)
    CONFIDENCE_CORRECT = float(os.getenv("CONFIDENCE_CORRECT", "0.8"))
    # 정답 판정 최소 정확도 — 이 값 이상이어야 '정답' 처리

    CONFIDENCE_CANDIDATE = float(os.getenv("CONFIDENCE_CANDIDATE", "0.5"))
    # 재학습 데이터 저장 최소 정확도 — 이 값 이상이어야 keypoint_store에 저장

    # 로그
    LOG_LEVEL = os.getenv("LOG_LEVEL", "INFO")
    LOG_FILE = os.getenv("LOG_FILE", "ops_server.log")
