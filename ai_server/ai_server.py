"""
AI 서버
- 운용서버로부터 TCP 9100으로 keypoint 수신
- Transformer 모델로 추론
- 결과 반환: {word_id, confidence, predicted_word}
"""

import asyncio
import json
import numpy as np
import torch
from pathlib import Path

from model import build_model
from datetime import datetime

# ── 설정 ───────────────────────────────────────────────
HOST        = "0.0.0.0"   # 모든 인터페이스에서 수신
PORT        = 9100         # 운용서버 ↔ AI서버 포트
MODEL_PATH  = Path("data/models/best_model.pt")
LABELS_PATH = Path("data/processed/labels.npy")
NUM_CLASSES = 1000
DEVICE      = torch.device("cuda" if torch.cuda.is_available() else "cpu")


class AIServer:
    """
    AI 추론 서버
    - asyncio 기반 비동기 TCP 서버
    - 여러 운용서버 요청 동시 처리 가능
    """

    def __init__(self):
        self.model  = None
        self.labels = None  # 라벨 번호 → 단어명 매핑

    def load_model(self):
        """모델 및 라벨 로드"""
        print(f"모델 로드 중... ({DEVICE})")

        # 모델 구조 생성 후 가중치 로드
        self.model = build_model(NUM_CLASSES).to(DEVICE)
        checkpoint = torch.load(MODEL_PATH, map_location=DEVICE)
        self.model.load_state_dict(checkpoint['model_state_dict'])
        self.model.eval()  # 추론 모드

        # 라벨 로드
        self.labels = np.load(LABELS_PATH, allow_pickle=True)

        print(f"모델 로드 완료 (val_acc: {checkpoint['val_acc']:.4f})")

    @torch.no_grad()
    def infer(self, keypoints: list) -> dict:
        """
        keypoint 시퀀스 추론
        keypoints: [[x, y, x, y, ...] × T프레임] 형태의 리스트
        반환: {word_id, word, confidence}
        """
        # numpy → tensor 변환
        seq = np.array(keypoints, dtype=np.float32)  # (T, 134)
        x   = torch.tensor(seq).unsqueeze(0).to(DEVICE)  # (1, T, 134)

        # 추론
        logits = self.model(x)                    # (1, 1000)
        probs  = torch.softmax(logits, dim=1)     # 확률값으로 변환
        confidence, pred_idx = probs.max(dim=1)   # 가장 높은 확률

        word_id   = int(pred_idx.item())
        word_name = str(self.labels[word_id])
        conf      = float(confidence.item())

        return {
            "word_id":    word_id,
            "word":       word_name,
            "confidence": conf
        }

    async def handle_client(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter):
        """
        운용서버 연결 처리
        프로토콜: 4바이트 길이 헤더 + JSON 바디
        """
        addr = writer.get_extra_info('peername')
        now  = datetime.now().strftime("%H:%M:%S")
        print(f"[{now}] 운용서버 연결: {addr}")

        try:
            while True:
                # 1. 4바이트 헤더 읽기 (메시지 길이)
                header  = await reader.readexactly(4)
                msg_len = int.from_bytes(header, byteorder='big')

                # 2. JSON 바디 읽기
                body = await reader.readexactly(msg_len)
                data = json.loads(body.decode('utf-8'))

                # 3. 추론
                keypoints = data.get('keypoints')  # [[x,y,...] × T]
                if keypoints is None:
                    response = {"error": "keypoints 없음"}
                else:
                    response = self.infer(keypoints)

                now = datetime.now().strftime("%H:%M:%S")
                print(f"[{now}] 추론 결과: {response['word']} (confidence: {response['confidence']:.4f})")

                # 4. 결과 반환 (4바이트 헤더 + JSON)
                resp_body   = json.dumps(response, ensure_ascii=False).encode('utf-8')
                resp_header = len(resp_body).to_bytes(4, byteorder='big')
                writer.write(resp_header + resp_body)
                await writer.drain()

        except asyncio.IncompleteReadError:
            now = datetime.now().strftime("%H:%M:%S")
            print(f"[{now}] 운용서버 연결 종료: {addr}")
        except Exception as e:
            now = datetime.now().strftime("%H:%M:%S")
            print(f"[{now}] 오류 발생: {e}")
        finally:
            writer.close()

    async def run(self):
        """서버 실행"""
        self.load_model()
        server = await asyncio.start_server(self.handle_client, HOST, PORT)
        print(f"AI 서버 시작 → {HOST}:{PORT}")
        async with server:
            await server.serve_forever()


if __name__ == "__main__":
    ai_server = AIServer()
    asyncio.run(ai_server.run())