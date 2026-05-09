"""
AI 서버
- 운용서버로부터 TCP 9100으로 keypoint 수신
- GRU 모델로 추론
- 결과 반환: {predicted_word_id, confidence, inference_ms}
- 모델 무중단 교체 지원
"""

import asyncio
import json
import time
import numpy as np
import torch
from pathlib import Path
from datetime import datetime

from model import build_gru_model  # ✅ GRU로 변경

# ── 설정 ───────────────────────────────────────────────
HOST        = "0.0.0.0"
PORT        = 9100
MODEL_PATH  = Path("data/models/best_model.pt")
LABELS_PATH = Path("data/processed/labels.npy")
NUM_CLASSES = 1000
DEVICE      = torch.device("cuda" if torch.cuda.is_available() else "cpu")

# 운용서버 프로토콜 타입명
REQ_AI_INFER = "REQ_AI_INFER"
RES_AI_INFER = "RES_AI_INFER"
REQ_RELOAD   = "REQ_MODEL_DEPLOY"  # NO.604


def now() -> str:
    """현재 시간 문자열 반환 (로그용)"""
    return datetime.now().strftime("%H:%M:%S")


class AIServer:
    """
    AI 추론 서버
    - asyncio 기반 비동기 TCP 서버
    - 여러 운용서버 요청 동시 처리 가능
    - 모델 무중단 교체 지원
    """

    def __init__(self):
        self.model  = None
        self.labels = None  # 클래스 인덱스 → 단어명 매핑
        self.lock   = asyncio.Lock()  # 모델 교체 중 추론 방지

    def load_model(self, model_path: Path = MODEL_PATH):
        """
        모델 및 라벨 로드
        model_path: 로드할 모델 파일 경로
        """
        print(f"[{now()}] 모델 로드 중... ({DEVICE})")

        model = build_gru_model(NUM_CLASSES).to(DEVICE)  # ✅ GRU
        checkpoint = torch.load(model_path, map_location=DEVICE)
        model.load_state_dict(checkpoint['model_state_dict'])
        model.eval()

        self.model  = model
        self.labels = np.load(LABELS_PATH, allow_pickle=True)

        print(f"[{now()}] 모델 로드 완료 (val_acc: {checkpoint['val_acc']:.4f})")

    async def reload_model(self, model_path: Path = MODEL_PATH):
        """
        모델 무중단 교체
        새 모델 로드 완료 후 교체 → 추론 중단 없음
        """
        print(f"[{now()}] 모델 교체 시작...")

        new_model = build_gru_model(NUM_CLASSES).to(DEVICE)  # ✅ GRU
        checkpoint = torch.load(model_path, map_location=DEVICE)
        new_model.load_state_dict(checkpoint['model_state_dict'])
        new_model.eval()

        async with self.lock:
            self.model = new_model
            print(f"[{now()}] 모델 교체 완료 (val_acc: {checkpoint['val_acc']:.4f})")

    @torch.no_grad()
    def infer(self, frames: list) -> dict:
        """
        keypoint 시퀀스 추론
        frames: [[x, y, ...] × T프레임] 형태 (T, 134)
        반환: {predicted_word_id, confidence} — DB id 기준 (클래스 인덱스 + 1)
        """
        t0 = time.monotonic()

        # numpy → tensor 변환
        seq = np.array(frames, dtype=np.float32)      # (T, 134)
        x   = torch.tensor(seq).unsqueeze(0).to(DEVICE)  # (1, T, 134)

        # padding_mask 없음 (단일 시퀀스, 패딩 불필요)
        padding_mask = torch.zeros(1, x.shape[1], dtype=torch.bool).to(DEVICE)

        # 추론
        logits     = self.model(x, padding_mask)       # (1, 1000)
        probs      = torch.softmax(logits, dim=1)
        confidence, pred_idx = probs.max(dim=1)

        class_idx         = int(pred_idx.item())
        predicted_word_id = class_idx + 1              # ✅ 클래스 인덱스 → DB id 변환
        conf              = float(confidence.item())
        inference_ms      = int((time.monotonic() - t0) * 1000)

        word_name = str(self.labels[class_idx])        # 로그용
        print(f"[{now()}] 추론: {word_name} (word_id={predicted_word_id}, confidence={conf:.4f}, {inference_ms}ms)")

        return {
            "predicted_word_id": predicted_word_id,    # ✅ DB id (1~1000)
            "confidence":        round(conf, 4),
            "inference_ms":      inference_ms,
        }

    async def handle_client(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter):
        """
        운용서버 연결 처리
        프로토콜: 4바이트 길이 헤더 + JSON 바디
        """
        addr = writer.get_extra_info('peername')
        print(f"[{now()}] 운용서버 연결: {addr}")

        try:
            while True:
                # 1. 4바이트 헤더 읽기 (메시지 길이)
                header  = await reader.readexactly(4)
                msg_len = int.from_bytes(header, byteorder='big')

                # 2. JSON 바디 읽기
                body = await reader.readexactly(msg_len)
                data = json.loads(body.decode('utf-8'))

                req_type   = data.get('type', '')
                request_id = data.get('request_id', '')

                if req_type == REQ_AI_INFER:
                    # 추론 요청 (NO.501)
                    frames = data.get('frames')
                    if not frames:
                        response = {"type": RES_AI_INFER, "request_id": request_id, "error": "frames 없음"}
                    else:
                        async with self.lock:
                            result = self.infer(frames)
                        response = {
                            "type":              RES_AI_INFER,
                            "request_id":        request_id,   # ✅ 운용서버 매칭용
                            "predicted_word_id": result["predicted_word_id"],
                            "confidence":        result["confidence"],
                            "inference_ms":      result["inference_ms"],
                        }

                elif req_type == REQ_RELOAD:
                    # 모델 교체 요청 (NO.604)
                    model_path = data.get('new_model_path', str(MODEL_PATH))
                    await self.reload_model(Path(model_path))
                    response = {"status": "ok", "message": "모델 교체 완료"}

                else:
                    response = {"error": f"알 수 없는 요청 타입: {req_type}"}

                # 3. 결과 반환 (4바이트 헤더 + JSON)
                resp_body   = json.dumps(response, ensure_ascii=False).encode('utf-8')
                resp_header = len(resp_body).to_bytes(4, byteorder='big')
                writer.write(resp_header + resp_body)
                await writer.drain()

        except asyncio.IncompleteReadError:
            print(f"[{now()}] 운용서버 연결 종료: {addr}")
        except Exception as e:
            print(f"[{now()}] 오류 발생: {e}")
        finally:
            writer.close()

    async def run(self):
        """서버 실행"""
        self.load_model()
        server = await asyncio.start_server(self.handle_client, HOST, PORT)
        print(f"[{now()}] AI 서버 시작 → {HOST}:{PORT}")
        async with server:
            await server.serve_forever()


if __name__ == "__main__":
    ai_server = AIServer()
    asyncio.run(ai_server.run())