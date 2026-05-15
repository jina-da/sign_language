"""
handlers/health.py  —  헬스체크 핸들러 (NO.701 ~ 702)

담당 메시지:
  NO.701 REQ_PING → NO.702 RES_PONG   AI 서버 생존 확인

REQ_PING 은 body 가 없어 payload_size=0 으로 전송된다.
protocol.py 의 recv_message 가 size=0 을 수신하면
{"type": "REQ_PING"} 딕셔너리를 반환하므로 여기서는 DB 조회 후 응답만 하면 된다.
"""

from logger import get_logger
from protocol import MessageType
from handlers.base import BaseHandler

logger = get_logger("handler.health")


class HealthHandler(BaseHandler):

    # ── NO.701 REQ_PING → NO.702 RES_PONG ────────────────────

    async def handle_ping(self, msg: dict, session_id: str) -> dict:
        """
        AI 서버(또는 클라이언트)가 운용 서버 생존을 확인한다.
        현재 활성 모델 버전을 함께 반환해 버전 동기화에 활용한다.

        NO.701: body 없음 (bodySize=0)
        NO.702 응답 필드: model_version_id
        """
        # 현재 is_active=1 인 모델의 버전 ID 조회
        model_version_id = await self.db.get_active_model_version_id()

        logger.debug(f"[Ping] REQ_PING 수신 → RES_PONG model_version_id={model_version_id}")
        return {
            "type"            : MessageType.RES_PONG,
            "model_version_id": model_version_id,  # NO.702: 클라이언트 버전 동기화용
        }
