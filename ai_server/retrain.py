"""
재학습 파이프라인
- keypoint_store에서 미학습 데이터 로드 (DB 연동)
- 신뢰도 기준 필터링 (50% 미만 버림, 50% 이상 학습)
- 기존 모델 백업 후 Fine-tuning
- 성능 비교 후 자동 배포 or 롤백
- 재학습 완료 후 is_trained=1 마킹
"""

import torch
import torch.nn as nn
import numpy as np
import json
import shutil
import pymysql
import os
from pathlib import Path
from datetime import datetime
from torch.utils.data import DataLoader
from dotenv import load_dotenv

load_dotenv()  # .env 파일 로드

from model import build_gru_model
from train import SignLanguageDataset, collate_fn, evaluate

# ── 경로 설정 ───────────────────────────────────────────
MODEL_DIR     = Path("data/models")
CURRENT_MODEL = MODEL_DIR / "best_model.pt"
BACKUP_DIR    = MODEL_DIR / "backup"
NUM_CLASSES   = 1000
DEVICE        = torch.device("cuda" if torch.cuda.is_available() else "cpu")

# ── Fine-tuning 하이퍼파라미터 ──────────────────────────
FINETUNE_EPOCHS = 10   # 전체 학습보다 적게
BATCH_SIZE      = 32
LR              = 1e-5  # Fine-tuning은 학습률 낮게

# ── DB 접속 설정 ────────────────────────────────────────
DB_CONFIG = {
    "host":     os.getenv("DB_HOST", "10.10.10.114"),
    "port":     int(os.getenv("DB_PORT", 3306)),
    "user":     os.getenv("DB_USER", "jina"),
    "password": os.getenv("DB_PASSWORD", ""),
    "database": os.getenv("DB_NAME", "ksl_learning"),
    "charset":  "utf8mb4",
}


# ── 유틸 ────────────────────────────────────────────────

def now() -> str:
    """현재 시간 문자열 반환 (로그용)"""
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


# ── DB 연동 ─────────────────────────────────────────────

def load_from_db() -> tuple:
    """
    keypoint_store에서 미학습 데이터(is_trained=0) 로드.
    keypoint_data: list[list[float]] 형태 JSON (프레임 × 134차원)
    반환: (sequences, labels, confidences, store_ids)
            - sequences:   np.ndarray(object) — 각 원소가 (T, 134) float32
            - labels:      np.ndarray(int32)  — 클래스 인덱스 (word_id - 1)
            - confidences: np.ndarray(float32)
            - store_ids:   list[int]           — is_trained 업데이트용
    """
    conn = pymysql.connect(**DB_CONFIG)
    try:
        with conn.cursor() as cur:
            cur.execute("""
                SELECT id, word_id, keypoint_data, confidence
                FROM   keypoint_store
                WHERE  is_trained = 0
                ORDER  BY created_at ASC
            """)
            rows = cur.fetchall()
    finally:
        conn.close()

    if not rows:
        print(f"[{now()}] DB 미학습 데이터 없음")
        return None, None, None, []

    store_ids   = []
    sequences   = []
    labels      = []
    confidences = []

    for row in rows:
        store_id, word_id, keypoint_json, confidence = row
        try:
            # keypoint_data: list[list[float]] → (T, 134) numpy array
            frames = json.loads(keypoint_json)
            seq    = np.array(frames, dtype=np.float32)  # shape: (T, 134)

            if seq.ndim != 2 or seq.shape[1] != 134:
                print(f"[{now()}] 형태 오류 (id={store_id}): shape={seq.shape} → 스킵")
                continue

            store_ids.append(store_id)
            sequences.append(seq)
            labels.append(word_id - 1)          # DB id → 클래스 인덱스
            confidences.append(float(confidence))

        except Exception as e:
            print(f"[{now()}] keypoint 파싱 실패 (id={store_id}): {e}")
            continue

    print(f"[{now()}] DB 로드 완료: {len(sequences)}개 (전체 {len(rows)}개 중 파싱 성공)")
    return (
        np.array(sequences,   dtype=object),
        np.array(labels,      dtype=np.int32),
        np.array(confidences, dtype=np.float32),
        store_ids,
    )


def mark_as_trained(store_ids: list[int]) -> None:
    """
    재학습에 사용된 데이터 is_trained=1로 업데이트.
    재학습 성공/롤백 모두 마킹 (어차피 학습에 사용한 데이터)
    """
    if not store_ids:
        return

    conn = pymysql.connect(**DB_CONFIG)
    try:
        with conn.cursor() as cur:
            placeholders = ",".join(["%s"] * len(store_ids))
            cur.execute(
                f"UPDATE keypoint_store SET is_trained = 1 WHERE id IN ({placeholders})",
                store_ids,
            )
        conn.commit()
    finally:
        conn.close()

    print(f"[{now()}] is_trained=1 마킹 완료: {len(store_ids)}개")


# ── 파이프라인 함수 ─────────────────────────────────────

def filter_by_confidence(
    sequences:   np.ndarray,
    labels:      np.ndarray,
    confidences: np.ndarray,
) -> tuple:
    """
    신뢰도 기준 데이터 필터링 (FixMatch 참고)
    - confidence < 0.5  → 버림 (너무 불확실)
    - confidence >= 0.5 → 학습 사용 (오답이어도 포함)
    """
    mask           = confidences >= 0.5
    filtered_seqs  = sequences[mask]
    filtered_lbls  = labels[mask]

    total    = len(sequences)
    kept     = mask.sum()
    dropped  = total - kept
    print(f"[{now()}] 필터링: {total}개 → {kept}개 사용 / {dropped}개 버림 (confidence < 0.5)")

    return filtered_seqs, filtered_lbls


def backup_model(version: str) -> Path:
    """
    현재 best_model.pt 백업.
    version: 백업 식별자 (예: v1.1_before)
    반환: 백업 파일 경로
    """
    BACKUP_DIR.mkdir(parents=True, exist_ok=True)
    backup_path = BACKUP_DIR / f"model_{version}.pt"
    shutil.copy(CURRENT_MODEL, backup_path)
    print(f"[{now()}] 모델 백업 완료 → {backup_path}")
    return backup_path


def get_current_accuracy() -> float:
    """현재 best_model.pt의 val_acc 반환"""
    checkpoint = torch.load(CURRENT_MODEL, map_location=DEVICE)
    return float(checkpoint["val_acc"])


def finetune(
    sequences: np.ndarray,
    labels:    np.ndarray,
    version:   str,
) -> tuple[float, Path]:
    """
    현재 모델 기반 Fine-tuning 수행.
    sequences: (N,) object array, 각 원소 (T, 134) float32
    labels:    (N,) int32 — 클래스 인덱스 (0~999)
    version:   저장할 모델 버전명
    반환: (best_val_acc, new_model_path)
    """
    print(f"[{now()}] Fine-tuning 시작 (데이터: {len(sequences)}개, device: {DEVICE})")

    # 현재 모델 가중치 로드
    model      = build_gru_model(NUM_CLASSES).to(DEVICE)
    checkpoint = torch.load(CURRENT_MODEL, map_location=DEVICE)
    model.load_state_dict(checkpoint["model_state_dict"])

    # 8:2 분리 (재학습용 train / 성능 측정용 val)
    total      = len(sequences)
    train_size = int(total * 0.8)
    val_size   = total - train_size

    dataset              = SignLanguageDataset(sequences, labels)
    train_set, val_set   = torch.utils.data.random_split(
        dataset, [train_size, val_size],
        generator=torch.Generator().manual_seed(42),
    )

    train_loader = DataLoader(train_set, batch_size=BATCH_SIZE, shuffle=True,  collate_fn=collate_fn)
    val_loader   = DataLoader(val_set,   batch_size=BATCH_SIZE, shuffle=False, collate_fn=collate_fn)

    optimizer = torch.optim.Adam(model.parameters(), lr=LR)
    criterion = nn.CrossEntropyLoss()

    best_val_acc = 0.0
    best_state   = None

    for epoch in range(1, FINETUNE_EPOCHS + 1):
        # ── train ──────────────────────────────────────
        model.train()
        for seqs, lbls, padding_mask in train_loader:
            seqs         = seqs.to(DEVICE)
            lbls         = lbls.to(DEVICE)
            padding_mask = padding_mask.to(DEVICE)

            optimizer.zero_grad()
            outputs = model(seqs, padding_mask)
            loss    = criterion(outputs, lbls)
            loss.backward()
            torch.nn.utils.clip_grad_norm_(model.parameters(), max_norm=1.0)
            optimizer.step()

        # ── val ────────────────────────────────────────
        val_loss, val_acc = evaluate(model, val_loader, criterion)
        print(f"[{now()}] Epoch {epoch:2d}/{FINETUNE_EPOCHS} | val_acc: {val_acc:.4f}")

        if val_acc > best_val_acc:
            best_val_acc = val_acc
            best_state   = {k: v.clone() for k, v in model.state_dict().items()}

    # 새 모델 저장
    new_model_path = MODEL_DIR / f"model_{version}.pt"
    torch.save(
        {
            "epoch":            FINETUNE_EPOCHS,
            "model_state_dict": best_state,
            "val_acc":          best_val_acc,
            "version":          version,
        },
        new_model_path,
    )
    print(f"[{now()}] Fine-tuning 완료 → {new_model_path} (val_acc: {best_val_acc:.4f})")
    return best_val_acc, new_model_path


def retrain_pipeline(
    sequences:   np.ndarray,
    labels:      np.ndarray,
    confidences: np.ndarray,
    version:     str,
) -> dict:
    """
    전체 재학습 파이프라인.
    1. 신뢰도 필터링
    2. 현재 모델 정확도 확인 + 백업
    3. Fine-tuning
    4. 성능 비교 → 배포(deployed) or 롤백(rollback)
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

    # 4. 성능 비교 → 배포 or 롤백
    if new_acc > old_acc:
        shutil.copy(new_model_path, CURRENT_MODEL)
        print(f"[{now()}] 모델 배포 완료 ({old_acc:.4f} → {new_acc:.4f})")
        status = "deployed"
    else:
        # 기존 모델 그대로 유지 (새 모델 파일은 보존)
        print(f"[{now()}] 성능 하락 → 롤백 유지 ({old_acc:.4f} → {new_acc:.4f})")
        status = "rollback"

    return {
        "status":  status,
        "old_acc": round(old_acc, 4),
        "new_acc": round(new_acc, 4),
        "version": version,
    }


# ── 진입점 ──────────────────────────────────────────────

if __name__ == "__main__":
    print(f"[{now()}] ===== 재학습 파이프라인 시작 =====")

    # 1. DB에서 미학습 데이터 로드
    sequences, labels, confidences, store_ids = load_from_db()

    if sequences is None or len(sequences) == 0:
        print(f"[{now()}] 미학습 데이터 없음 → 종료")
        exit(0)

    # 2. 재학습 파이프라인 실행
    result = retrain_pipeline(
        sequences, labels, confidences,
        version="v1.1",   # TODO: DB model_versions에서 버전 자동 채번
    )
    print(f"[{now()}] 파이프라인 결과: {result}")

    # 3. 사용한 데이터 is_trained=1 마킹 (성공/롤백 모두)
    if result["status"] in ("deployed", "rollback"):
        mark_as_trained(store_ids)

    print(f"[{now()}] ===== 재학습 파이프라인 종료 =====")