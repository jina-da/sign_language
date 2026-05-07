"""
학습 스크립트
- 전처리된 데이터 로드 (sequences.npy, label_indices.npy)
- 8:1:1 비율로 train/val/test 분리
- Transformer 모델 학습
- epoch마다 val accuracy 확인
- 가장 좋은 모델 저장
"""

import numpy as np
import torch
import torch.nn as nn
from torch.utils.data import Dataset, DataLoader, random_split
from torch.nn.utils.rnn import pad_sequence
from pathlib import Path
import time

from model import build_model


# ── 경로 설정 ──────────────────────────────────────────
DATA_DIR   = Path("data/processed")
OUTPUT_DIR = Path("data/models")
OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

# ── 하이퍼파라미터 ─────────────────────────────────────
BATCH_SIZE  = 32
NUM_EPOCHS  = 50
LR          = 1e-4       # Adam optimizer 초기 학습률
NUM_CLASSES = 1000
DEVICE      = torch.device("cuda" if torch.cuda.is_available() else "cpu")


class SignLanguageDataset(Dataset):
    """
    수화 keypoint 시퀀스 데이터셋
    가변 길이 시퀀스를 처리하기 위해 길이 정보도 저장
    """

    def __init__(self, sequences: np.ndarray, labels: np.ndarray):
        self.sequences = sequences  # (N,) object array, 각 원소: (T, 134)
        self.labels    = labels     # (N,) int32

    def __len__(self) -> int:
        return len(self.sequences)

    def __getitem__(self, idx: int) -> tuple:
        # object array일 경우 float32로 변환
        seq   = torch.tensor(np.array(self.sequences[idx], dtype=np.float32), dtype=torch.float32)
        label = torch.tensor(self.labels[idx], dtype=torch.long)
        return seq, label


def collate_fn(batch: list) -> tuple:
    """
    가변 길이 시퀀스를 배치로 묶는 함수
    짧은 시퀀스는 0으로 패딩, padding_mask로 패딩 위치 표시
    """
    seqs, labels = zip(*batch)

    # 패딩: 가장 긴 시퀀스 기준으로 0 채우기
    # pad_sequence: (T, 134) → (max_T, batch, 134) → transpose → (batch, max_T, 134)
    padded = pad_sequence(seqs, batch_first=True, padding_value=0.0)  # (batch, max_T, 134)

    # padding_mask: 패딩된 위치 True (Transformer에서 무시됨)
    lengths = torch.tensor([s.shape[0] for s in seqs])
    max_len = padded.shape[1]
    padding_mask = torch.arange(max_len).unsqueeze(0) >= lengths.unsqueeze(1)  # (batch, max_T)

    labels = torch.stack(labels)  # (batch,)
    return padded, labels, padding_mask


def train_one_epoch(
    model: nn.Module,
    loader: DataLoader,
    optimizer: torch.optim.Optimizer,
    criterion: nn.Module
) -> tuple[float, float]:
    """
    1 epoch 학습
    반환: (평균 loss, accuracy)
    """
    model.train()
    total_loss = 0.0
    correct    = 0
    total      = 0

    for seqs, labels, padding_mask in loader:
        seqs         = seqs.to(DEVICE)
        labels       = labels.to(DEVICE)
        padding_mask = padding_mask.to(DEVICE)

        optimizer.zero_grad()
        outputs = model(seqs, padding_mask)          # (batch, 1000)
        loss    = criterion(outputs, labels)
        loss.backward()
        optimizer.step()

        total_loss += loss.item()
        preds      = outputs.argmax(dim=1)
        correct    += (preds == labels).sum().item()
        total      += labels.size(0)

    return total_loss / len(loader), correct / total


@torch.no_grad()
def evaluate(
    model: nn.Module,
    loader: DataLoader,
    criterion: nn.Module
) -> tuple[float, float]:
    """
    검증/테스트 평가
    반환: (평균 loss, accuracy)
    """
    model.eval()
    total_loss = 0.0
    correct    = 0
    total      = 0

    for seqs, labels, padding_mask in loader:
        seqs         = seqs.to(DEVICE)
        labels       = labels.to(DEVICE)
        padding_mask = padding_mask.to(DEVICE)

        outputs = model(seqs, padding_mask)
        loss    = criterion(outputs, labels)

        total_loss += loss.item()
        preds      = outputs.argmax(dim=1)
        correct    += (preds == labels).sum().item()
        total      += labels.size(0)

    return total_loss / len(loader), correct / total


def main():
    print(f"device: {DEVICE}")

    # ── 데이터 로드 ────────────────────────────────────
    sequences = np.load(DATA_DIR / "sequences_aug.npy",     allow_pickle=True)
    labels    = np.load(DATA_DIR / "label_indices_aug.npy")
    print(f"총 샘플 수: {len(sequences)}")

    # ── 데이터셋 분리 8:1:1 ────────────────────────────
    dataset    = SignLanguageDataset(sequences, labels)
    total      = len(dataset)
    train_size = int(total * 0.8)
    val_size   = int(total * 0.1)
    test_size  = total - train_size - val_size

    train_set, val_set, test_set = random_split(
        dataset, [train_size, val_size, test_size],
        generator=torch.Generator().manual_seed(42)  # 재현성
    )
    print(f"train: {train_size} | val: {val_size} | test: {test_size}")

    # ── DataLoader ─────────────────────────────────────
    train_loader = DataLoader(train_set, batch_size=BATCH_SIZE, shuffle=True,  collate_fn=collate_fn)
    val_loader   = DataLoader(val_set,   batch_size=BATCH_SIZE, shuffle=False, collate_fn=collate_fn)
    test_loader  = DataLoader(test_set,  batch_size=BATCH_SIZE, shuffle=False, collate_fn=collate_fn)

    # ── 모델, 옵티마이저, 손실함수 ─────────────────────
    model     = build_model(NUM_CLASSES).to(DEVICE)
    optimizer = torch.optim.Adam(model.parameters(), lr=LR)
    criterion = nn.CrossEntropyLoss()

    # 학습률 스케줄러: val loss 개선 없으면 lr 줄이기
    scheduler = torch.optim.lr_scheduler.ReduceLROnPlateau(
        optimizer, mode='min', patience=5, factor=0.5
    )

    # ── 학습 루프 ──────────────────────────────────────
    best_val_acc = 0.0

    for epoch in range(1, NUM_EPOCHS + 1):
        start = time.time()

        train_loss, train_acc = train_one_epoch(model, train_loader, optimizer, criterion)
        val_loss,   val_acc   = evaluate(model, val_loader, criterion)
        scheduler.step(val_loss)

        elapsed = time.time() - start
        print(
            f"Epoch {epoch:3d}/{NUM_EPOCHS} | "
            f"train loss: {train_loss:.4f} acc: {train_acc:.4f} | "
            f"val loss: {val_loss:.4f} acc: {val_acc:.4f} | "
            f"{elapsed:.1f}s"
        )

        # 가장 좋은 모델 저장
        if val_acc > best_val_acc:
            best_val_acc = val_acc
            torch.save({
                'epoch': epoch,
                'model_state_dict': model.state_dict(),
                'optimizer_state_dict': optimizer.state_dict(),
                'val_acc': val_acc,
            }, OUTPUT_DIR / "best_model.pt")
            print(f"  → best model 저장 (val_acc: {val_acc:.4f})")

    # ── 최종 테스트 ────────────────────────────────────
    checkpoint = torch.load(OUTPUT_DIR / "best_model.pt")
    model.load_state_dict(checkpoint['model_state_dict'])
    test_loss, test_acc = evaluate(model, test_loader, criterion)
    print(f"\n최종 test accuracy: {test_acc:.4f}")


if __name__ == "__main__":
    main()