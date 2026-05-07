"""
재학습 파이프라인
- 운용서버로부터 재학습 트리거 신호 수신 (TCP 9100)
- keypoint_store에서 미학습 데이터 로드
- 신뢰도 기준 필터링 (50% 미만 버림, 50~80% 오답도 학습)
- 기존 모델 백업 후 Fine-tuning
- 성능 비교 후 자동 배포 or 롤백
"""

import torch
import torch.nn as nn
import numpy as np
import json
import shutil
from pathlib import Path
from datetime import datetime
from torch.utils.data import DataLoader
from torch.nn.utils.rnn import pad_sequence

from model import build_model
from train import SignLanguageDataset, collate_fn, evaluate

# ── 설정 ───────────────────────────────────────────────
MODEL_DIR    = Path("data/models")
CURRENT_MODEL = MODEL_DIR / "best_model.pt"
BACKUP_DIR   = MODEL_DIR / "backup"
LABELS_PATH  = Path("data/processed/labels.npy")
NUM_CLASSES  = 1000
DEVICE       = torch.device("cuda" if torch.cuda.is_available() else "cpu")

# 재학습 하이퍼파라미터
FINETUNE_EPOCHS = 10    # Fine-tuning은 전체 학습보다 적게
BATCH_SIZE      = 32
LR              = 1e-5  # Fine-tuning은 학습률 낮게


def now() -> str:
    """현재 시간 문자열 반환 (로그용)"""
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def backup_model(version: str) -> Path:
    """
    현재 모델 백업
    version: 버전명 (예: v1.1)
    반환: 백업 파일 경로
    """
    BACKUP_DIR.mkdir(parents=True, exist_ok=True)
    backup_path = BACKUP_DIR / f"model_{version}.pt"
    shutil.copy(CURRENT_MODEL, backup_path)
    print(f"[{now()}] 모델 백업 완료 → {backup_path}")
    return backup_path


def filter_by_confidence(
    sequences: np.ndarray,
    labels: np.ndarray,
    confidences: np.ndarray
) -> tuple:
    """
    신뢰도 기준 데이터 필터링
    - confidence < 0.5 → 버림 (너무 불확실)
    - confidence >= 0.5 → 학습에 사용 (오답이어도 포함)
    FixMatch 논문 참고
    """
    mask = confidences >= 0.5
    filtered_seqs   = sequences[mask]
    filtered_labels = labels[mask]

    total    = len(sequences)
    filtered = mask.sum()
    dropped  = total - filtered
    print(f"[{now()}] 필터링: {total}개 → {filtered}개 사용 / {dropped}개 버림 (confidence < 0.5)")

    return filtered_seqs, filtered_labels


def get_current_accuracy() -> float:
    """현재 모델의 정확도 반환"""
    checkpoint = torch.load(CURRENT_MODEL, map_location=DEVICE)
    return checkpoint['val_acc']


def finetune(
    sequences: np.ndarray,
    labels: np.ndarray,
    version: str
) -> float:
    """
    Fine-tuning 수행
    sequences: 새 keypoint 시퀀스
    labels: 정답 라벨
    version: 새 모델 버전명
    반환: 새 모델의 val_accuracy
    """
    print(f"[{now()}] Fine-tuning 시작 (데이터: {len(sequences)}개)")

    # 기존 모델 로드
    model = build_model(NUM_CLASSES).to(DEVICE)
    checkpoint = torch.load(CURRENT_MODEL, map_location=DEVICE)
    model.load_state_dict(checkpoint['model_state_dict'])

    # 데이터 분리 8:2 (재학습은 검증만)
    total      = len(sequences)
    train_size = int(total * 0.8)
    val_size   = total - train_size

    dataset = SignLanguageDataset(sequences, labels)
    train_set, val_set = torch.utils.data.random_split(
        dataset, [train_size, val_size],
        generator=torch.Generator().manual_seed(42)
    )

    train_loader = DataLoader(train_set, batch_size=BATCH_SIZE, shuffle=True,  collate_fn=collate_fn)
    val_loader   = DataLoader(val_set,   batch_size=BATCH_SIZE, shuffle=False, collate_fn=collate_fn)

    optimizer = torch.optim.Adam(model.parameters(), lr=LR)
    criterion = nn.CrossEntropyLoss()

    best_val_acc  = 0.0
    best_state    = None

    for epoch in range(1, FINETUNE_EPOCHS + 1):
        # 학습
        model.train()
        for seqs, lbls, padding_mask in train_loader:
            seqs         = seqs.to(DEVICE)
            lbls         = lbls.to(DEVICE)
            padding_mask = padding_mask.to(DEVICE)

            optimizer.zero_grad()
            outputs = model(seqs, padding_mask)
            loss    = criterion(outputs, lbls)
            loss.backward()
            optimizer.step()

        # 검증
        val_loss, val_acc = evaluate(model, val_loader, criterion)
        print(f"[{now()}] Epoch {epoch}/{FINETUNE_EPOCHS} | val_acc: {val_acc:.4f}")

        if val_acc > best_val_acc:
            best_val_acc = val_acc
            best_state   = model.state_dict().copy()

    # 새 모델 저장
    new_model_path = MODEL_DIR / f"model_{version}.pt"
    torch.save({
        'epoch':            FINETUNE_EPOCHS,
        'model_state_dict': best_state,
        'val_acc':          best_val_acc,
        'version':          version
    }, new_model_path)

    print(f"[{now()}] Fine-tuning 완료 (val_acc: {best_val_acc:.4f})")
    return best_val_acc, new_model_path


def retrain_pipeline(
    sequences: np.ndarray,
    labels: np.ndarray,
    confidences: np.ndarray,
    version: str
) -> dict:
    """
    전체 재학습 파이프라인
    1. 신뢰도 필터링
    2. 기존 모델 백업
    3. Fine-tuning
    4. 성능 비교 → 배포 or 롤백
    반환: {status, old_acc, new_acc, version}
    """
    # 1. 신뢰도 필터링
    filtered_seqs, filtered_labels = filter_by_confidence(sequences, labels, confidences)

    if len(filtered_seqs) == 0:
        return {"status": "skip", "reason": "필터링 후 데이터 없음"}

    # 2. 기존 모델 정확도 확인 + 백업
    old_acc = get_current_accuracy()
    print(f"[{now()}] 기존 모델 정확도: {old_acc:.4f}")
    backup_model(f"{version}_before")

    # 3. Fine-tuning
    new_acc, new_model_path = finetune(filtered_seqs, filtered_labels, version)

    # 4. 성능 비교
    if new_acc > old_acc:
        # 성능 향상 → 배포
        shutil.copy(new_model_path, CURRENT_MODEL)
        print(f"[{now()}] 모델 배포 완료 ({old_acc:.4f} → {new_acc:.4f})")
        status = "deployed"
    else:
        # 성능 하락 → 롤백 (새 모델 보존, 기존 유지)
        print(f"[{now()}] 성능 하락 → 롤백 ({old_acc:.4f} → {new_acc:.4f})")
        status = "rollback"

    return {
        "status":  status,
        "old_acc": old_acc,
        "new_acc": new_acc,
        "version": version
    }


if __name__ == "__main__":
    # 테스트용 더미 데이터로 파이프라인 검증
    print(f"[{now()}] 재학습 파이프라인 테스트 시작")

    # 더미 데이터 생성 (실제로는 DB에서 가져옴)
    dummy_seqs   = np.array([np.random.randn(50, 134).astype(np.float32) for _ in range(100)], dtype=object)
    dummy_labels = np.random.randint(0, NUM_CLASSES, size=100).astype(np.int32)
    dummy_confs  = np.random.uniform(0.3, 0.9, size=100).astype(np.float32)

    result = retrain_pipeline(dummy_seqs, dummy_labels, dummy_confs, version="v1.1")
    print(f"[{now()}] 결과: {result}")