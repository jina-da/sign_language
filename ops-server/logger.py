"""
SignLearn 로거 설정 모듈
- 파일 로테이션 (10MB × 5개 보존)
- 콘솔/파일 동시 출력
- 모듈별 전용 logger 반환
- DEBUG/INFO/WARNING/ERROR/CRITICAL 레벨별 색상 (콘솔)
- 함수명·줄번호·예외 트레이스백 자동 포함
"""

import logging
import logging.handlers
import os
import sys
from pathlib import Path

# ── 설정 ────────────────────────────────────────────────────
LOG_DIR      = Path(os.getenv("LOG_DIR", str(Path(__file__).parent / "logs")))
LOG_FILE     = LOG_DIR / "ops_server.log"
LOG_LEVEL    = os.getenv("LOG_LEVEL", "DEBUG").upper()   # 기본 DEBUG (개발 중)
MAX_BYTES    = 10 * 1024 * 1024   # 10 MB
BACKUP_COUNT = 5                  # 최대 5개 보존 (ops_server.log.1 ~ .5)

LOG_DIR.mkdir(exist_ok=True)

# ── 포맷 ────────────────────────────────────────────────────
# 파일: 시간 | 레벨 | 모듈명 | 함수명:줄번호 | 메시지
FILE_FORMAT = (
    "%(asctime)s | %(levelname)-8s | %(name)-18s | "
    "%(funcName)s:%(lineno)d | %(message)s"
)
# 콘솔: 시간(짧게) | 레벨(색상) | 모듈명 | 메시지
CONSOLE_FORMAT = "%(asctime)s | %(levelname)-8s | %(name)-18s | %(message)s"
DATE_FORMAT = "%Y-%m-%d %H:%M:%S"


# ── ANSI 색상 (콘솔 전용) ───────────────────────────────────
class ColorFormatter(logging.Formatter):
    COLORS = {
        "DEBUG":    "\033[36m",   # Cyan
        "INFO":     "\033[32m",   # Green
        "WARNING":  "\033[33m",   # Yellow
        "ERROR":    "\033[31m",   # Red
        "CRITICAL": "\033[35m",   # Magenta
    }
    RESET = "\033[0m"

    def format(self, record: logging.LogRecord) -> str:
        color = self.COLORS.get(record.levelname, "")
        record.levelname = f"{color}{record.levelname}{self.RESET}"
        return super().format(record)


# ── 핸들러 공유 (중복 설정 방지) ────────────────────────────
_initialized = False

def _setup_root_logger():
    global _initialized
    if _initialized:
        return
    _initialized = True

    root = logging.getLogger("signlearn")
    root.setLevel(getattr(logging, LOG_LEVEL, logging.DEBUG))
    root.propagate = False

    # 파일 핸들러 (로테이션)
    fh = logging.handlers.RotatingFileHandler(
        LOG_FILE,
        maxBytes=MAX_BYTES,
        backupCount=BACKUP_COUNT,
        encoding="utf-8",
    )
    fh.setLevel(logging.DEBUG)
    fh.setFormatter(logging.Formatter(FILE_FORMAT, datefmt=DATE_FORMAT))

    # 콘솔 핸들러 (색상)
    ch = logging.StreamHandler(sys.stdout)
    ch.setLevel(logging.DEBUG)
    ch.setFormatter(ColorFormatter(CONSOLE_FORMAT, datefmt=DATE_FORMAT))

    root.addHandler(fh)
    root.addHandler(ch)


def get_logger(name: str) -> logging.Logger:
    """
    모듈별 logger 반환.
    사용법: logger = get_logger(__name__)
    """
    _setup_root_logger()
    return logging.getLogger(f"signlearn.{name}")
