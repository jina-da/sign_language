"""
SignLearn 재학습 스케줄러

변경 내용 (명세서 기준):
  ✅ REQ_RETRAIN(NO.601): trigger, data_path, data_count, hyperparams 필드 추가
  ✅ RES_RETRAIN_ACK(NO.602): 재학습 수신 확인 처리 추가
  ✅ NOTIFY_RETRAIN_DONE(NO.603): acc_before, acc_after, improved, new_model_path 파싱
  ✅ improved=true → REQ_MODEL_DEPLOY(NO.604) 전송
  ✅ improved=false → REQ_MODEL_ROLLBACK(NO.606) 전송
  ✅ RES_MODEL_DEPLOY(NO.605) / RES_MODEL_ROLLBACK(NO.607) 응답 처리
  ✅ logger.py 중앙 로거 사용
"""

import asyncio
import time
from datetime import datetime, timedelta

from config import ServerConfig
from logger import get_logger

logger = get_logger("retrain_scheduler")


class RetrainScheduler:
    def __init__(self, config: ServerConfig, db_manager, model_manager):
        self.config = config
        self.db = db_manager
        self.model_manager = model_manager
        self._retrain_lock = asyncio.Lock()  # 중복 재학습 방지용 락
        self._retrain_count = 0              # 누적 재학습 횟수 (통계용)

    # ──────────────────────────────────────────────────────────
    # 스케줄 루프
    # ──────────────────────────────────────────────────────────

    async def run(self):
        """
        서버 기동 시 백그라운드 태스크로 실행.
        config.RETRAIN_HOUR 시각마다 재학습 조건을 체크한다.
        """
        logger.info(
            f"[Retrain] 스케줄러 시작 — "
            f"트리거시각={self.config.RETRAIN_HOUR:02d}:00 "
            f"데이터임계={self.config.RETRAIN_DATA_THRESHOLD}개"
        )
        while True:
            await self._wait_until_next_trigger()   # 다음 트리거 시각까지 대기
            await self._check_and_trigger()          # 조건 확인 후 재학습 실행

    async def _wait_until_next_trigger(self):
        """
        오늘(또는 내일) config.RETRAIN_HOUR 시각까지 sleep.
        이미 해당 시각을 지났으면 내일 같은 시각으로 설정한다.
        """
        now = datetime.now()
        target = now.replace(
            hour=self.config.RETRAIN_HOUR, minute=0, second=0, microsecond=0
        )
        if now >= target:
            target += timedelta(days=1)  # 오늘 이미 지났으면 내일로

        wait_sec = (target - now).total_seconds()
        logger.info(
            f"[Retrain] 다음 체크: {target.strftime('%Y-%m-%d %H:%M:%S')} "
            f"({wait_sec/3600:.1f}시간 후)"
        )
        await asyncio.sleep(wait_sec)

    # ──────────────────────────────────────────────────────────
    # 트리거 조건 체크
    # ──────────────────────────────────────────────────────────

    async def _check_and_trigger(self):
        """
        미학습 Keypoint 수가 임계값을 넘으면 AI 서버에 재학습 신호를 전송한다.
        이미 재학습 중이면 이번 트리거는 건너뛴다 (중복 방지).
        """
        logger.info("[Retrain] 트리거 조건 체크 시작")

        # 재학습이 이미 진행 중이면 skip
        if self._retrain_lock.locked():
            logger.warning("[Retrain] 재학습이 이미 진행 중 — 이번 트리거 건너뜀")
            return

        async with self._retrain_lock:
            try:
                # DB 에서 is_trained=0 인 keypoint 수 조회
                count = await self.db.count_untrained_keypoints()
                threshold = self.config.RETRAIN_DATA_THRESHOLD

                logger.info(
                    f"[Retrain] 미학습 Keypoint: {count}/{threshold}개 "
                    f"({'충족' if count >= threshold else '부족'})"
                )

                if count < threshold:
                    # 데이터가 부족하면 다음 트리거까지 대기
                    logger.info(f"[Retrain] 데이터 부족({count} < {threshold}) — 다음 트리거까지 대기")
                    return

                logger.info(f"[Retrain] 조건 충족 → AI 서버 재학습 신호 전송 (data={count})")
                await self._send_retrain_signal(count)

            except Exception as e:
                logger.error(
                    f"[Retrain] 체크/트리거 중 예외: {type(e).__name__}: {e}",
                    exc_info=True
                )

    # ──────────────────────────────────────────────────────────
    # 재학습 신호 전송 (메인 흐름)
    # ──────────────────────────────────────────────────────────

    async def _send_retrain_signal(self, data_count: int):
        """
        AI 서버와 1:1 TCP 연결을 맺고 아래 순서로 메시지를 교환한다.

        [운용서버 → AI서버]  NO.601 REQ_RETRAIN        재학습 시작 명령
        [AI서버  → 운용서버] NO.602 RES_RETRAIN_ACK    재학습 수신 확인
            ... AI 서버가 학습 완료할 때까지 대기 (최대 3시간) ...
        [AI서버  → 운용서버] NO.603 NOTIFY_RETRAIN_DONE 재학습 완료 알림
            improved=True  → NO.604 REQ_MODEL_DEPLOY   신규 모델 배포 승인
            improved=False → NO.606 REQ_MODEL_ROLLBACK 이전 버전 롤백 명령
        """
        from protocol import Protocol, MessageType

        logger.info(
            f"[Retrain] AI 서버({self.config.AI_HOST}:{self.config.AI_PORT})에 "
            f"재학습 신호 전송 중..."
        )
        t0 = time.monotonic()

        try:
            # AI 서버에 TCP 연결 (10초 타임아웃)
            reader, writer = await asyncio.wait_for(
                asyncio.open_connection(self.config.AI_HOST, self.config.AI_PORT),
                timeout=10.0,
            )
            logger.info(f"[Retrain] AI 서버 연결 성공 (대기={int((time.monotonic()-t0)*1000)}ms)")

            # ── NO.601 REQ_RETRAIN 전송 ───────────────────────────
            triggered_at = datetime.utcnow().isoformat()
            keypoints    = await self.db.get_untrained_keypoints()   # 학습 대상 keypoint 목록
            data_path    = f"/data/kp/{triggered_at[:10]}"           # 날짜 기반 데이터 경로

            await Protocol.send_message(writer, {
                "type": MessageType.REQ_RETRAIN,
                "trigger"    : "scheduled",   # NO.601: 트리거 종류 (scheduled | manual)
                "data_path"  : data_path,     # NO.601: AI 서버가 읽을 keypoint 데이터 경로
                "data_count" : data_count,    # NO.601: 학습 데이터 수
                "hyperparams": {              # NO.601: 하이퍼파라미터
                    "lr"    : 0.0001,
                    "epochs": 10,
                },
            })
            logger.info(
                f"[Retrain] NO.601 REQ_RETRAIN 전송 완료: "
                f"data_count={data_count} data_path={data_path}"
            )

            # ── NO.602 RES_RETRAIN_ACK 수신 ──────────────────────
            # AI 서버가 학습 명령을 정상 수신했는지 확인 (30초 내)
            ack = await asyncio.wait_for(Protocol.recv_message(reader), timeout=30.0)

            if not ack or ack.get("type") != MessageType.RES_RETRAIN_ACK:
                # 예상한 타입이 아니면 비정상 응답으로 중단
                logger.error(f"[Retrain] NO.602 RES_RETRAIN_ACK 수신 실패: {ack}")
                writer.close()
                return

            logger.info(f"[Retrain] NO.602 RES_RETRAIN_ACK 수신: status={ack.get('status')}")

            # ── NO.603 NOTIFY_RETRAIN_DONE 대기 ──────────────────
            # 실제 학습 완료 알림까지 최대 3시간(10800초) 대기
            logger.info("[Retrain] NO.603 NOTIFY_RETRAIN_DONE 대기 중 (최대 3시간)...")
            notify = await asyncio.wait_for(
                Protocol.recv_message(reader), timeout=10800.0
            )
            elapsed_min = int((time.monotonic() - t0) / 60)
            logger.info(f"[Retrain] NO.603 NOTIFY_RETRAIN_DONE 수신 (소요={elapsed_min}분)")

            if not notify or notify.get("type") != MessageType.NOTIFY_RETRAIN_DONE:
                logger.error(f"[Retrain] 예상치 못한 응답: {notify}")
                writer.close()
                return

            # ── NO.603 필드 파싱 ──────────────────────────────────
            acc_before     = notify.get("acc_before", 0.0)    # 학습 전 정확도
            acc_after      = notify.get("acc_after",  0.0)    # 학습 후 정확도
            improved       = notify.get("improved",   False)  # 성능 향상 여부
            new_model_path = notify.get("new_model_path", "") # 신규 모델 파일 경로

            logger.info(
                f"[Retrain] 결과: acc_before={acc_before:.4f} → acc_after={acc_after:.4f} "
                f"improved={improved} new_model_path={new_model_path}"
            )

            if improved:
                # 성능이 향상됐으면 신규 모델 배포 (NO.604 → NO.605)
                await self._send_model_deploy(writer, reader, new_model_path, acc_after)
            else:
                # 성능이 그대로이거나 하락했으면 이전 버전으로 롤백 (NO.606 → NO.607)
                current = await self.db.get_active_model_version()
                target_version_id = current["id"] if current else 1
                await self._send_model_rollback(writer, reader, target_version_id)

            writer.close()
            await writer.wait_closed()

            # 배포가 완료된 경우에만 keypoint_store 에 is_trained=1 업데이트
            trained_ids = [kp["id"] for kp in keypoints]
            if trained_ids and improved:
                await self.db.mark_keypoints_trained(trained_ids)
                logger.info(f"[Retrain] keypoint_store {len(trained_ids)}개 is_trained=1 업데이트")

            self._retrain_count += 1  # 누적 횟수 증가

        except asyncio.TimeoutError as e:
            logger.error(f"[Retrain] 타임아웃: {e} — AI 서버 응답 없음 또는 연결 실패", exc_info=True)
        except ConnectionRefusedError:
            logger.error(
                f"[Retrain] AI 서버 연결 거부 "
                f"({self.config.AI_HOST}:{self.config.AI_PORT}) — 기동 확인 필요"
            )
        except Exception as e:
            logger.error(f"[Retrain] 신호 전송 중 예외: {type(e).__name__}: {e}", exc_info=True)

    # ──────────────────────────────────────────────────────────
    # 모델 배포 (NO.604 ~ NO.605)
    # ──────────────────────────────────────────────────────────

    async def _send_model_deploy(
        self,
        writer,
        reader,
        new_model_path: str,
        acc_after: float,
    ):
        """
        성능이 향상된 신규 모델을 AI 서버에 배포 요청한다.

        [운용서버 → AI서버] NO.604 REQ_MODEL_DEPLOY  신규 모델 배포 승인
        [AI서버  → 운용서버] NO.605 RES_MODEL_DEPLOY 배포 완료 (model_version_id 반환)

        배포 완료 후 DB 의 model_versions 테이블을 최신 버전으로 갱신한다.
        """
        from protocol import Protocol, MessageType

        logger.info(f"[Retrain] NO.604 REQ_MODEL_DEPLOY 전송: new_model_path={new_model_path}")

        await Protocol.send_message(writer, {
            "type"           : MessageType.REQ_MODEL_DEPLOY,
            "new_model_path" : new_model_path,   # NO.604: 배포할 모델 파일 경로
        })

        # NO.605 RES_MODEL_DEPLOY 수신 (60초 내)
        deploy_res = await asyncio.wait_for(Protocol.recv_message(reader), timeout=60.0)

        if not deploy_res or deploy_res.get("type") != MessageType.RES_MODEL_DEPLOY:
            logger.error(f"[Retrain] NO.605 RES_MODEL_DEPLOY 수신 실패: {deploy_res}")
            return

        new_version_id = deploy_res.get("model_version_id")  # NO.605: 배포된 모델 버전 ID
        logger.info(
            f"[Retrain] ✅ NO.605 RES_MODEL_DEPLOY 수신: "
            f"model_version_id={new_version_id} 배포 완료"
        )

        # DB 에 신규 모델 버전 등록 및 활성화
        try:
            await self.db.add_model_version(
                version   = str(new_version_id),
                accuracy  = acc_after,
                file_path = new_model_path,
                is_active = True,   # 기존 활성 버전은 DB 내에서 자동 비활성화됨
            )
            logger.info(f"[Retrain] DB 모델 버전 등록 완료: version_id={new_version_id}")
        except Exception as e:
            logger.error(f"[Retrain] 배포 후 DB 업데이트 실패: {e}", exc_info=True)

    # ──────────────────────────────────────────────────────────
    # 모델 롤백 (NO.606 ~ NO.607)
    # ──────────────────────────────────────────────────────────

    async def _send_model_rollback(
        self,
        writer,
        reader,
        target_version_id: int,
    ):
        """
        성능이 향상되지 않은 경우 AI 서버에 이전 모델로 롤백을 지시한다.

        [운용서버 → AI서버] NO.606 REQ_MODEL_ROLLBACK 롤백 명령
        [AI서버  → 운용서버] NO.607 RES_MODEL_ROLLBACK 롤백 완료 (model_version_id 반환)
        """
        from protocol import Protocol, MessageType

        logger.warning(
            f"[Retrain] ⚠️ 성능 미향상 — NO.606 REQ_MODEL_ROLLBACK 전송: "
            f"target_version_id={target_version_id}"
        )

        await Protocol.send_message(writer, {
            "type"             : MessageType.REQ_MODEL_ROLLBACK,
            "target_version_id": target_version_id,  # NO.606: 복원할 모델 버전 ID
        })

        # NO.607 RES_MODEL_ROLLBACK 수신 (60초 내)
        rollback_res = await asyncio.wait_for(Protocol.recv_message(reader), timeout=60.0)

        if not rollback_res or rollback_res.get("type") != MessageType.RES_MODEL_ROLLBACK:
            logger.error(f"[Retrain] NO.607 RES_MODEL_ROLLBACK 수신 실패: {rollback_res}")
            return

        rolled_version_id = rollback_res.get("model_version_id")  # NO.607: 실제 롤백된 버전 ID
        logger.warning(
            f"[Retrain] NO.607 RES_MODEL_ROLLBACK 수신: "
            f"model_version_id={rolled_version_id} 롤백 완료"
        )
