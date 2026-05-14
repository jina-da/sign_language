"""
전처리 스크립트
- SYN 1,000단어 기준 필터링
- morpheme JSON에서 수화 구간 추출
- keypoint JSON에서 해당 프레임만 읽어 정규화
- 16명 × 1,000단어 = 16,000개 샘플 저장
"""

import json
import csv
import numpy as np
from pathlib import Path

# ── 경로 설정 ──────────────────────────────────────────
BASE_DIR     = Path("data/raw/WORD/004.수어영상/1.Training/라벨링데이터/REAL/WORD")
MORPHEME_DIR = Path("data/raw/WORD/morpheme")
SYN_CSV      = Path("data/syn_단어목록.csv")
OUTPUT_DIR   = Path("data/processed")

# ── 상수 ───────────────────────────────────────────────
FPS     = 30
IMG_W   = 1920
IMG_H   = 1080
PERSONS = [f"{i:02d}" for i in range(1, 17)]  # 01~16


def load_syn_words(csv_path: Path) -> dict:
    """SYN CSV에서 {단어번호: 단어명} 반환"""
    words = {}
    with open(csv_path, encoding='utf-8-sig') as f:
        reader = csv.DictReader(f)
        for row in reader:
            words[row['단어번호']] = row['단어']
    return words


def get_frame_range(morpheme_path: Path) -> tuple | None:
    """morpheme JSON에서 수화 구간 프레임 번호 반환"""
    with open(morpheme_path) as f:
        data = json.load(f)
    if not data['data']:
        return None
    segment = data['data'][0]
    start_frame = int(segment['start'] * FPS)
    end_frame   = int(segment['end']   * FPS)
    return start_frame, end_frame


def extract_keypoints(frame_path: Path) -> np.ndarray:
    """keypoint JSON 1프레임 → 정규화된 좌표 배열 (134,) 반환
    pose 25개 + hand_left 21개 + hand_right 21개 = 67개 × 2(x,y) = 134
    """
    with open(frame_path) as f:
        data = json.load(f)
    p = data['people']

    result = []
    for key in ['pose_keypoints_2d', 'hand_left_keypoints_2d', 'hand_right_keypoints_2d']:
        coords = p[key]
        xs = coords[0::3]  # confidence 제외
        ys = coords[1::3]
        for x, y in zip(xs, ys):
            result.extend([x / IMG_W, y / IMG_H])

    return np.array(result, dtype=np.float32)


def process_sample(word_num: str, person: str) -> np.ndarray | None:
    """단어 1개 × 사람 1명 → keypoint 시퀀스 (T, 134) 반환"""
    morpheme_path = MORPHEME_DIR / person / f"NIA_SL_{word_num}_REAL{person}_F_morpheme.json"
    keypoint_dir  = BASE_DIR / person / f"NIA_SL_{word_num}_REAL{person}_F"

    if not morpheme_path.exists() or not keypoint_dir.exists():
        return None

    frame_range = get_frame_range(morpheme_path)
    if frame_range is None:
        return None

    start_frame, end_frame = frame_range
    frames = []
    for frame_idx in range(start_frame, end_frame + 1):
        frame_file = keypoint_dir / f"NIA_SL_{word_num}_REAL{person}_F_{frame_idx:012d}_keypoints.json"
        if not frame_file.exists():
            continue
        frames.append(extract_keypoints(frame_file))

    if len(frames) == 0:
        return None

     # 최소 5프레임 이상인 샘플만 사용
    if len(frames) < 5:
        return None

    return np.array(frames, dtype=np.float32)  # (T, 134)



def main():
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    syn_words   = load_syn_words(SYN_CSV)
    word_to_idx = {k: i for i, k in enumerate(sorted(syn_words.keys()))}
    print(f"SYN 단어 수: {len(syn_words)}")

    sequences  = []
    label_idxs = []
    total = len(syn_words) * len(PERSONS)
    skip  = 0

    for done, (word_num, _) in enumerate(sorted(syn_words.items()), 1):
        label_idx = word_to_idx[word_num]
        for person in PERSONS:
            seq = process_sample(word_num, person)
            if seq is None:
                skip += 1
                continue
            sequences.append(seq)
            label_idxs.append(label_idx)

        if done % 100 == 0:
            print(f"진행: {done * len(PERSONS)}/{total} | 스킵: {skip}")

    # 저장
    label_names = np.array([syn_words[k] for k in sorted(syn_words.keys())])
    np.save(OUTPUT_DIR / "labels.npy",        label_names)
    np.save(OUTPUT_DIR / "sequences.npy",     np.array(sequences, dtype=object))
    np.save(OUTPUT_DIR / "label_indices.npy", np.array(label_idxs, dtype=np.int32))

    print(f"\n완료: {len(sequences)}개 샘플 저장 → {OUTPUT_DIR}")
    print(f"스킵: {skip}개")


if __name__ == "__main__":
    main()