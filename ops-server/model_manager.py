"""
SignLearn 모델 관리자 - 디버그 로깅 강화본
변경 내용:
  ✅ logger.py 중앙 로거 사용
  ✅ 모델 수신 진행상황 (파일 크기, 수신 완료) 로그
  ✅ 크기 초과 / 타임아웃 / 기타 예외 상세 로그
  ✅ 모델 교체·롤백 신호 전송 결과 로그
"""

import asyncio
import struct
from pathlib import Path
from typing import Optional

from config import ServerConfig
from logger import get_logger
from protocol import Protocol, MessageType

logger = get_logger("model_manager")

# 수정 5: 상대경로 → __file__ 기준 절대경로 (파이참 Working Directory 무관)
MODEL_DIR = Path(__file__).parent / "models"
MODEL_DIR.mkdir(exist_ok=True)

MAX_MODEL_SIZE = 500 * 1024 * 1024  # 500 MB


class ModelManager:
    def __init__(self, config: ServerConfig):
        self.config = config
        self._current_version: Optional[str] = None

    async def receive_model_from_ai(
        self,
        reader: asyncio.StreamReader,
        writer: asyncio.StreamWriter,
        version: str,
        accuracy: float,
    ) -> Optional[Path]:
        logger.info(f"[Model] 모델 수신 시작: version={version} accuracy={accuracy:.4f}")

        try:
            header = await reader.readexactly(4)
            file_size = struct.unpack(">I", header)[0]
            logger.info(f"[Model] 수신 예정 크기: {file_size / 1024 / 1024:.1f} MB")

            if file_size > MAX_MODEL_SIZE:
                logger.error(
                    f"[Model] 모델 크기 초과: {file_size / 1024 / 1024:.1f} MB "
                    f"(최대={MAX_MODEL_SIZE / 1024 / 1024:.0f} MB) — 수신 중단"
                )
                return None

            logger.debug(f"[Model] 바이너리 수신 중 (timeout=120s)...")
            data = await asyncio.wait_for(
                reader.readexactly(file_size), timeout=120.0
            )

            file_path = MODEL_DIR / f"model_{version}.pt"
            file_path.write_bytes(data)
            actual_mb = len(data) / 1024 / 1024
            logger.info(
                f"[Model] 모델 저장 완료: {file_path} "
                f"({actual_mb:.1f} MB) version={version}"
            )
            self._current_version = version
            return file_path

        except asyncio.TimeoutError:
            logger.error(
                "[Model] 모델 수신 타임아웃 (120s) — "
                "AI 서버 전송 속도 또는 네트워크 확인 필요"
            )
            return None
        except asyncio.IncompleteReadError as e:
            logger.error(
                f"[Model] 수신 중 연결 끊김: 예상={e.expected} 수신={len(e.partial)}bytes",
                exc_info=True
            )
            return None
        except Exception as e:
            logger.error(
                f"[Model] 수신 중 예외: {type(e).__name__}: {e}",
                exc_info=True
            )
            return None

    async def notify_model_reload(
        self, ai_writer: asyncio.StreamWriter, file_path: Path
    ):
        logger.info(f"[Model] AI 서버에 모델 교체(reload) 신호 전송: {file_path}")
        try:
            await Protocol.send_message(
                ai_writer,
                {
                    "type": MessageType.AI_MODEL_ACK,
                    "action": "reload",
                    "model_path": str(file_path),
                },
            )
            logger.info(f"[Model] 모델 교체 신호 전송 완료: {file_path}")
        except Exception as e:
            logger.error(
                f"[Model] 모델 교체 신호 전송 실패: {type(e).__name__}: {e}",
                exc_info=True
            )

    async def rollback(self, ai_writer: asyncio.StreamWriter, fallback_path: str):
        logger.warning(f"[Model] AI 서버에 롤백(rollback) 신호 전송: {fallback_path}")
        try:
            await Protocol.send_message(
                ai_writer,
                {
                    "type": MessageType.AI_MODEL_ACK,
                    "action": "rollback",
                    "model_path": fallback_path,
                },
            )
            logger.warning(f"[Model] 롤백 신호 전송 완료: {fallback_path}")
        except Exception as e:
            logger.error(
                f"[Model] 롤백 신호 전송 실패: {type(e).__name__}: {e}",
                exc_info=True
            )
