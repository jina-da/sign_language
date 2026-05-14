# compare_keypoints.py
import numpy as np
from pathlib import Path

def cosine_similarity(a, b):
    """두 시퀀스 평균 벡터 간 코사인 유사도"""
    a_mean = np.mean(a, axis=0)
    b_mean = np.mean(b, axis=0)
    return np.dot(a_mean, b_mean) / (np.linalg.norm(a_mean) * np.linalg.norm(b_mean))

def load_train_sample(word_id: int, n_samples: int = 3):
    """학습 데이터에서 특정 word_id 샘플 추출"""
    sequences   = np.load("data/processed/sequences.npy", allow_pickle=True)
    label_idxs  = np.load("data/processed/label_indices.npy")
    labels      = np.load("data/processed/labels.npy", allow_pickle=True)

    # word_id → 클래스 인덱스 변환
    class_idx = word_id - 1
    word_name = str(labels[class_idx])

    # 해당 클래스 샘플 추출
    mask    = label_idxs == class_idx
    samples = sequences[mask][:n_samples]

    print(f"단어: {word_name} (word_id={word_id}, class_idx={class_idx})")
    print(f"학습 샘플 수: {mask.sum()}개 중 {len(samples)}개 사용")
    return samples, word_name

def load_test_keypoint(json_path: str):
    import json
    with open(json_path) as f:
        data = json.load(f)

    frames = data.get("frames", [])
    IMG_W, IMG_H = 1920.0, 1080.0

    # OpenPose → MediaPipe 순서 매핑
    OPENPOSE_TO_MEDIAPIPE_POSE = [
        0,16,16,16,15,15,15,18,17,0,0,5,2,6,3,7,4,7,4,7,4,7,4,12,9
    ]

    result = []
    for frame in frames:
        if frame.get("is_gongsu", False):
            continue

        # pose: MediaPipe 순서 그대로 (클라이언트가 이미 MediaPipe 순서로 줌)
        pose = [v / (IMG_W if j % 2 == 0 else IMG_H)
                for joint in frame.get("pose", [])
                for j, v in enumerate(joint[:2])]
        left_hand  = [v / (IMG_W if j % 2 == 0 else IMG_H)
                      for joint in frame.get("left_hand", [])
                      for j, v in enumerate(joint[:2])]
        right_hand = [v / (IMG_W if j % 2 == 0 else IMG_H)
                      for joint in frame.get("right_hand", [])
                      for j, v in enumerate(joint[:2])]
        flat = pose + left_hand + right_hand
        if len(flat) == 134:
            result.append(flat)

    return np.array(result, dtype=np.float32)

def main():
    # ── 설정 ──────────────────────────────────────
    TARGET_WORD_ID = 118        # 테스트한 단어 word_id (두부면 해당 id로 변경)
    TEST_JSON_PATH = "user1_word118_20260514_072659.json"  # 운용서버에서 저장한 JSON 경로
    # ──────────────────────────────────────────────

    # 학습 샘플 로드
    train_samples, word_name = load_train_sample(TARGET_WORD_ID, n_samples=5)

    # 테스트 keypoint 로드
    test_seq = load_test_keypoint(TEST_JSON_PATH)
    print(f"\n테스트 시퀀스 프레임 수: {len(test_seq)}")

    # 유사도 계산
    print(f"\n{'='*50}")
    print(f"[{word_name}] 학습 샘플 vs 테스트 keypoint 유사도")
    print(f"{'='*50}")
    sims = []
    for i, train_seq in enumerate(train_samples):
        sim = cosine_similarity(train_seq, test_seq)
        sims.append(sim)
        print(f"  학습 샘플 {i+1}: {sim:.4f}")

    print(f"\n평균 유사도: {np.mean(sims):.4f}")
    print(f"{'='*50}")

    # 해석
    avg = np.mean(sims)
    if avg >= 0.95:
        print("→ 매우 유사 (정상 범위)")
    elif avg >= 0.85:
        print("→ 어느 정도 유사 (허용 범위)")
    elif avg >= 0.70:
        print("→ 낮음 (OpenPose vs MediaPipe 분포 차이 가능성)")
    else:
        print("→ 매우 낮음 (입력 데이터 분포가 완전히 다름)")

if __name__ == "__main__":
    main()