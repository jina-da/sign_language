"""
Data Augmentation 스크립트
- 원본 데이터 × 5배 증강
- 방법 1: 노이즈 추가 (강도 약/강 2가지)
- 방법 2: 속도 변환 (빠르게/느리게 2가지)
- 결과: sequences.npy, label_indices.npy 덮어쓰기
"""

import numpy as np
from pathlib import Path
from scipy.interpolate import interp1d

DATA_DIR = Path("data/processed")


def add_noise(seq: np.ndarray, std: float) -> np.ndarray:
    """
    좌표에 가우시안 노이즈 추가
    seq: (T, 134)
    std: 노이즈 강도 (0.01 = 약, 0.02 = 강)
    """
    noise = np.random.normal(0, std, seq.shape).astype(np.float32)
    # 0~1 범위 클리핑 (정규화 범위 유지)
    return np.clip(seq + noise, 0.0, 1.0)


def time_warp(seq: np.ndarray, speed: float) -> np.ndarray:
    """
    시퀀스 속도 변환 (보간법 사용)
    seq: (T, 134)
    speed: 1.0보다 크면 빠르게(프레임 줄임), 작으면 느리게(프레임 늘림)
    """
    T = seq.shape[0]
    new_T = max(2, int(T / speed))  # 최소 2프레임 보장

    # 원본 프레임 인덱스 기준으로 보간
    old_indices = np.linspace(0, T - 1, T)
    new_indices = np.linspace(0, T - 1, new_T)

    # 각 차원별 보간
    warped = np.zeros((new_T, seq.shape[1]), dtype=np.float32)
    for dim in range(seq.shape[1]):
        f = interp1d(old_indices, seq[:, dim], kind='linear')
        warped[:, dim] = f(new_indices)

    return warped


def augment(sequences: np.ndarray, labels: np.ndarray) -> tuple:
    aug_seqs   = []
    aug_labels = []

    total = len(sequences)
    nan_count = 0

    for i, (seq, label) in enumerate(zip(sequences, labels)):
        # 1. 원본
        aug_seqs.append(seq)
        aug_labels.append(label)

        # 2. 노이즈 약
        aug_seqs.append(add_noise(seq, std=0.01))
        aug_labels.append(label)

        # 3. 노이즈 강
        aug_seqs.append(add_noise(seq, std=0.02))
        aug_labels.append(label)

        # 4. 빠르게 (NaN 체크)
        warped = time_warp(seq, speed=1.25)
        if not np.isnan(warped).any():
            aug_seqs.append(warped)
            aug_labels.append(label)
        else:
            aug_seqs.append(seq)  # NaN이면 원본으로 대체
            aug_labels.append(label)
            nan_count += 1

        # 5. 느리게 (NaN 체크)
        warped = time_warp(seq, speed=0.75)
        if not np.isnan(warped).any():
            aug_seqs.append(warped)
            aug_labels.append(label)
        else:
            aug_seqs.append(seq)  # NaN이면 원본으로 대체
            aug_labels.append(label)
            nan_count += 1

        if (i + 1) % 500 == 0:
            print(f"증강 진행: {i + 1}/{total}")

    print(f"NaN 발생으로 원본 대체: {nan_count}개")
    return (
        np.array(aug_seqs,   dtype=object),
        np.array(aug_labels, dtype=np.int32)
    )


def main():
    # 원본 데이터 로드
    sequences = np.load(DATA_DIR / "sequences.npy",     allow_pickle=True)
    labels    = np.load(DATA_DIR / "label_indices.npy")
    print(f"원본 샘플 수: {len(sequences)}")

    # 증강
    aug_seqs, aug_labels = augment(sequences, labels)
    print(f"증강 후 샘플 수: {len(aug_seqs)}")

    # 저장 (증강 데이터는 별도 파일로 저장)
    np.save(DATA_DIR / "sequences_aug.npy",     aug_seqs)
    np.save(DATA_DIR / "label_indices_aug.npy", aug_labels)
    print("저장 완료")


if __name__ == "__main__":
    main()