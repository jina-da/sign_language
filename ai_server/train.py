"""
학습 스크립트
- 원본 데이터 기준으로 train/val/test 분리 후 train만 증강
- 데이터 누수 방지 (같은 원본에서 나온 증강 데이터가 val/test에 섞이지 않게)
- Transformer 모델 학습
- epoch마다 val accuracy 확인
- 가장 좋은 모델 저장
"""

import numpy as np
import torch
import torch.nn as nn
from torch.utils.data import Dataset, DataLoader
from torch.nn.utils.rnn import pad_sequence
from pathlib import Path
from scipy.interpolate import interp1d
import time

from model import build_model, build_gru_model

# ── 경로 설정 ──────────────────────────────────────────
DATA_DIR   = Path("data/processed")
OUTPUT_DIR = Path("data/models")
OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

# ── 하이퍼파라미터 ─────────────────────────────────────
BATCH_SIZE  = 32
NUM_EPOCHS  = 100
LR          = 1e-4
NUM_CLASSES = 1000
DEVICE      = torch.device("cuda" if torch.cuda.is_available() else "cpu")


# ── 증강 함수 ──────────────────────────────────────────
def add_noise(seq: np.ndarray, std: float) -> np.ndarray:
    """좌표에 가우시안 노이즈 추가"""
    noise = np.random.normal(0, std, seq.shape).astype(np.float32)
    return np.clip(seq + noise, 0.0, 1.0)


def time_warp(seq: np.ndarray, speed: float) -> np.ndarray:
    """시퀀스 속도 변환"""
    T = seq.shape[0]
    new_T = max(5, int(T / speed))
    old_idx = np.linspace(0, T - 1, T)
    new_idx = np.linspace(0, T - 1, new_T)
    warped = np.zeros((new_T, seq.shape[1]), dtype=np.float32)
    for dim in range(seq.shape[1]):
        f = interp1d(old_idx, seq[:, dim], kind='linear')
        warped[:, dim] = f(new_idx)
    if np.isnan(warped).any():
        return seq
    return warped


def augment_sequences(sequences: np.ndarray, labels: np.ndarray) -> tuple:
    """train 데이터만 증강 (×5배)"""
    aug_seqs   = []
    aug_labels = []
    for seq, label in zip(sequences, labels):
        aug_seqs.append(seq)
        aug_labels.append(label)
        aug_seqs.append(add_noise(seq, std=0.01))
        aug_labels.append(label)
        aug_seqs.append(add_noise(seq, std=0.02))
        aug_labels.append(label)
        aug_seqs.append(time_warp(seq, speed=1.25))
        aug_labels.append(label)
        aug_seqs.append(time_warp(seq, speed=0.75))
        aug_labels.append(label)
    return np.array(aug_seqs, dtype=object), np.array(aug_labels, dtype=np.int32)


class SignLanguageDataset(Dataset):
    """수화 keypoint 시퀀스 데이터셋"""

    def __init__(self, sequences: np.ndarray, labels: np.ndarray):
        self.sequences = sequences
        self.labels    = labels

    def __len__(self) -> int:
        return len(self.sequences)

    def __getitem__(self, idx: int) -> tuple:
        seq   = torch.tensor(np.array(self.sequences[idx], dtype=np.float32), dtype=torch.float32)
        label = torch.tensor(self.labels[idx], dtype=torch.long)
        return seq, label


def collate_fn(batch: list) -> tuple:
    """가변 길이 시퀀스를 배치로 묶는 함수"""
    seqs, labels = zip(*batch)
    padded = pad_sequence(seqs, batch_first=True, padding_value=0.0)
    lengths = torch.tensor([s.shape[0] for s in seqs])
    max_len = padded.shape[1]
    padding_mask = torch.arange(max_len).unsqueeze(0) >= lengths.unsqueeze(1)
    labels = torch.stack(labels)
    return padded, labels, padding_mask


def train_one_epoch(
    model: nn.Module,
    loader: DataLoader,
    optimizer: torch.optim.Optimizer,
    criterion: nn.Module
) -> tuple[float, float]:
    """1 epoch 학습"""
    model.train()
    total_loss = 0.0
    correct    = 0
    total      = 0

    for seqs, labels, padding_mask in loader:
        seqs         = seqs.to(DEVICE)
        labels       = labels.to(DEVICE)
        padding_mask = padding_mask.to(DEVICE)

        optimizer.zero_grad()
        outputs = model(seqs, padding_mask)
        loss    = criterion(outputs, labels)
        loss.backward()
        # gradient exploding 방지
        torch.nn.utils.clip_grad_norm_(model.parameters(), max_norm=1.0)
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
    """검증/테스트 평가"""
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

    # ── 원본 데이터 로드 ───────────────────────────────
    sequences = np.load(DATA_DIR / "sequences.npy",     allow_pickle=True)
    labels    = np.load(DATA_DIR / "label_indices.npy")

    print(f"원본 샘플 수: {len(sequences)}")

    # ── 원본 기준으로 8:1:1 분리 ──────────────────────
    total      = len(sequences)
    indices    = np.random.default_rng(42).permutation(total)
    train_end  = int(total * 0.8)
    val_end    = int(total * 0.9)

    train_idx = indices[:train_end]
    val_idx   = indices[train_end:val_end]
    test_idx  = indices[val_end:]

    train_seqs   = sequences[train_idx]
    train_labels = labels[train_idx]
    val_seqs     = sequences[val_idx]
    val_labels   = labels[val_idx]
    test_seqs    = sequences[test_idx]
    test_labels  = labels[test_idx]

    # ── train만 증강 ───────────────────────────────────
    print("train 데이터 증강 중...")
    train_seqs, train_labels = augment_sequences(train_seqs, train_labels)
    print(f"train: {len(train_seqs)} | val: {len(val_seqs)} | test: {len(test_seqs)}")

    # ── DataLoader ─────────────────────────────────────
    train_loader = DataLoader(SignLanguageDataset(train_seqs, train_labels),
                              batch_size=BATCH_SIZE, shuffle=True, collate_fn=collate_fn)
    val_loader   = DataLoader(SignLanguageDataset(val_seqs, val_labels),
                              batch_size=BATCH_SIZE, shuffle=False, collate_fn=collate_fn)
    test_loader  = DataLoader(SignLanguageDataset(test_seqs, test_labels),
                              batch_size=BATCH_SIZE, shuffle=False, collate_fn=collate_fn)

    # ── 모델, 옵티마이저, 손실함수 ─────────────────────
    model = build_gru_model(NUM_CLASSES).to(DEVICE)
    optimizer = torch.optim.Adam(
        model.parameters(),
        lr=LR
    )
    criterion = nn.CrossEntropyLoss()

    # 학습률 스케줄러
    scheduler = torch.optim.lr_scheduler.ReduceLROnPlateau(
        optimizer, mode='min', patience=5, factor=0.7
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