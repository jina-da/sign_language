"""
keypoint_server.py — MediaPipe 0.10.x Tasks API 수정 버전
"""

import asyncio
import json
import struct
import cv2
import mediapipe as mp
from mediapipe.tasks import python as mp_python
from mediapipe.tasks.python.vision import HandLandmarker, HandLandmarkerOptions
from mediapipe.tasks.python.vision import PoseLandmarker, PoseLandmarkerOptions
from mediapipe.tasks.python.vision.core.vision_task_running_mode import VisionTaskRunningMode
import urllib.request
import os

# ── 설정 ──────────────────────────────────────────────────────
FRAME_PORT    = 7000
KEYPOINT_PORT = 7001
CONTROL_PORT  = 7002   # Qt → Python 제어 메시지 (우세손 설정 등)
CAMERA_INDEX  = 0
FRAME_WIDTH   = 640
FRAME_HEIGHT  = 480
JPEG_QUALITY  = 70

# 우세손 설정 (Qt에서 로그인 후 전달)
is_dominant_left = False

HAND_MODEL = "hand_landmarker.task"
POSE_MODEL = "pose_landmarker_lite.task"

# ── 모델 다운로드 ─────────────────────────────────────────────
def download_model(url, filename):
    if not os.path.exists(filename):
        print(f"[Model] 다운로드 중: {filename}")
        urllib.request.urlretrieve(url, filename)
        print(f"[Model] 완료: {filename}")
    else:
        print(f"[Model] 이미 존재: {filename}")

download_model(
    "https://storage.googleapis.com/mediapipe-models/hand_landmarker/hand_landmarker/float16/1/hand_landmarker.task",
    HAND_MODEL
)
download_model(
    "https://storage.googleapis.com/mediapipe-models/pose_landmarker/pose_landmarker_lite/float16/1/pose_landmarker_lite.task",
    POSE_MODEL
)

# ── MediaPipe 초기화 ──────────────────────────────────────────
hand_options = HandLandmarkerOptions(
    base_options=mp_python.BaseOptions(model_asset_path=HAND_MODEL),
    num_hands=2,
    min_hand_detection_confidence=0.5,
    min_hand_presence_confidence=0.5,
    min_tracking_confidence=0.5,
    running_mode=VisionTaskRunningMode.VIDEO
)
pose_options = PoseLandmarkerOptions(
    base_options=mp_python.BaseOptions(model_asset_path=POSE_MODEL),
    min_pose_detection_confidence=0.5,
    min_pose_presence_confidence=0.5,
    min_tracking_confidence=0.5,
    running_mode=VisionTaskRunningMode.VIDEO
)

hand_landmarker = HandLandmarker.create_from_options(hand_options)
pose_landmarker = PoseLandmarker.create_from_options(pose_options)

print("[MediaPipe] 초기화 완료")

# ── 패킷 전송 헬퍼 ───────────────────────────────────────────
async def send_packet(writer, data: bytes):
    header = struct.pack(">I", len(data))
    writer.write(header + data)
    await writer.drain()

async def send_json(writer, obj: dict):
    await send_packet(writer, json.dumps(obj).encode("utf-8"))

async def send_jpeg(writer, frame_bgr):
    _, jpeg = cv2.imencode(".jpg", frame_bgr,
                           [cv2.IMWRITE_JPEG_QUALITY, JPEG_QUALITY])
    await send_packet(writer, jpeg.tobytes())

# ── 관절 추출 헬퍼 ───────────────────────────────────────────
def extract_hand(hand_landmarks, handedness_list, label):
    for i, h in enumerate(handedness_list):
        # display_name 대신 category_name 사용
        if h[0].category_name == label:
            return [[lm.x, lm.y, lm.z] for lm in hand_landmarks[i]]
    return None  # 감지 안 됨을 None으로 구분

def hand_detected(hand_data):
    """손 데이터가 실제로 감지됐는지 확인"""
    return hand_data is not None

def hand_to_list(hand_data):
    """None이면 zeros 반환"""
    return hand_data if hand_data is not None else [[0.0, 0.0, 0.0]] * 21

def extract_pose(pose_landmarks):
    if not pose_landmarks:
        return [[0.0, 0.0, 0.0]] * 25
    return [[lm.x, lm.y, lm.visibility] for lm in pose_landmarks[0][:25]]

# ── 우세손 정규화 ─────────────────────────────────────────────
def normalize_dominant_hand(left_hand, right_hand):
    """
    왼손잡이일 경우: x좌표 반전(1-x) + 왼손↔오른손 채널 swap
    오른손잡이: 그대로 반환
    """
    if not is_dominant_left:
        return left_hand, right_hand

    # x좌표 반전
    def flip_x(hand):
        return [[1.0 - lm[0], lm[1], lm[2]] for lm in hand]

    # 채널 swap: 왼손 ↔ 오른손
    return flip_x(right_hand), flip_x(left_hand)

# ── 공수 자세 감지 ───────────────────────────────────────────
def is_gongsu_pose(left_hand, right_hand, pose_lms):
    """
    공수 자세: 양손이 몸 중앙에 가까이 모인 상태
    조건:
      1. 양손 모두 감지됨
      2. 두 손 wrist 간 거리가 가까움 (0.25 이하)
      3. 손 중앙이 어깨 중앙에서 너무 멀지 않음 (0.4 이하)
    """
    if not hand_detected(left_hand) or not hand_detected(right_hand):
        return False

    lx, ly = left_hand[0][0],  left_hand[0][1]
    rx, ry = right_hand[0][0], right_hand[0][1]

    hand_dist = ((lx - rx) ** 2 + (ly - ry) ** 2) ** 0.5
    if hand_dist > 0.25:
        return False

    cx = (lx + rx) / 2
    cy = (ly + ry) / 2
    if len(pose_lms) > 12:
        sx = (pose_lms[11][0] + pose_lms[12][0]) / 2
        sy = (pose_lms[11][1] + pose_lms[12][1]) / 2
        body_dist = ((cx - sx) ** 2 + (cy - sy) ** 2) ** 0.5
        if body_dist > 0.4:
            return False

    return True

# ── 클라이언트 관리 ───────────────────────────────────────────
frame_writers    = []
keypoint_writers = []

async def handle_frame_client(reader, writer):
    addr = writer.get_extra_info("peername")
    print(f"[Frame] Qt 연결: {addr}")
    frame_writers.append(writer)
    try:
        await reader.read(1024)
    except:
        pass
    finally:
        if writer in frame_writers:
            frame_writers.remove(writer)

async def handle_keypoint_client(reader, writer):
    addr = writer.get_extra_info("peername")
    print(f"[Keypoint] Qt 연결: {addr}")
    keypoint_writers.append(writer)
    try:
        await reader.read(1024)
    except:
        pass
    finally:
        if writer in keypoint_writers:
            keypoint_writers.remove(writer)

async def handle_control_client(reader, writer):
    """Qt에서 우세손 설정 등 제어 메시지 수신"""
    global is_dominant_left
    addr = writer.get_extra_info("peername")
    print(f"[Control] Qt 연결: {addr}")
    try:
        while True:
            # 4바이트 헤더 읽기
            header = await reader.readexactly(4)
            body_len = struct.unpack(">I", header)[0]
            body = await reader.readexactly(body_len)
            msg = json.loads(body.decode("utf-8"))

            if msg.get("type") == "set_dominant_hand":
                is_dominant_left = msg.get("is_dominant_left", False)
                print(f"[Control] 우세손 설정: {'왼손' if is_dominant_left else '오른손'}")
    except:
        pass
    finally:
        print(f"[Control] Qt 연결 종료: {addr}")

# ── 메인 캡처 루프 ────────────────────────────────────────────
async def capture_loop():
    cap = cv2.VideoCapture(CAMERA_INDEX)
    cap.set(cv2.CAP_PROP_FRAME_WIDTH,  FRAME_WIDTH)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, FRAME_HEIGHT)

    if not cap.isOpened():
        print("[Error] 카메라를 열 수 없습니다.")
        return

    print("[Camera] 캡처 시작")
    frame_idx    = 0
    timestamp_ms = 0

    while True:
        ret, frame = cap.read()
        if not ret:
            await asyncio.sleep(0.01)
            continue

        timestamp_ms += 33

        rgb      = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        mp_image = mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb)

        hand_result = hand_landmarker.detect_for_video(mp_image, timestamp_ms)
        pose_result = pose_landmarker.detect_for_video(mp_image, timestamp_ms)

        left_hand_raw  = extract_hand(hand_result.hand_landmarks,
                                      hand_result.handedness, "Left")
        right_hand_raw = extract_hand(hand_result.hand_landmarks,
                                      hand_result.handedness, "Right")

        # 우세손 정규화
        left_normalized, right_normalized = normalize_dominant_hand(
            hand_to_list(left_hand_raw),
            hand_to_list(right_hand_raw)
        )

        left_hand  = left_normalized
        right_hand = right_normalized
        pose_lms   = extract_pose(pose_result.pose_landmarks)

        gongsu = is_gongsu_pose(left_hand_raw, right_hand_raw, pose_lms)

        keypoint_data = {
            "frame_idx":  frame_idx,
            "left_hand":  left_hand,
            "right_hand": right_hand,
            "pose":       pose_lms,
            "is_gongsu":  gongsu
        }

        # 디버그: 3초마다 출력
        if frame_idx % 90 == 0:
            num_hands = len(hand_result.hand_landmarks)
            print(f"[Debug] 감지된 손 수: {num_hands}")
            for i, h in enumerate(hand_result.handedness):
                print(f"  손{i}: category_name={h[0].category_name} score={h[0].score:.2f}")
            print(f"[Debug] 왼손:{hand_detected(left_hand_raw)} 오른손:{hand_detected(right_hand_raw)} 공수:{gongsu}")
            if hand_detected(left_hand_raw) and hand_detected(right_hand_raw):
                lx, ly = left_hand_raw[0][0], left_hand_raw[0][1]
                rx, ry = right_hand_raw[0][0], right_hand_raw[0][1]
                hand_dist = ((lx-rx)**2 + (ly-ry)**2) ** 0.5
                print(f"  손간거리:{hand_dist:.3f} (기준:0.25)")
                if len(pose_lms) > 12:
                    cx=(lx+rx)/2; cy=(ly+ry)/2
                    sx=(pose_lms[11][0]+pose_lms[12][0])/2
                    sy=(pose_lms[11][1]+pose_lms[12][1])/2
                    body_dist = ((cx-sx)**2+(cy-sy)**2)**0.5
                    print(f"  몸중앙거리:{body_dist:.3f} (기준:0.4)")

        # 프레임 전송
        dead = [w for w in frame_writers
                if not await _try_send_jpeg(w, frame)]
        for w in dead: frame_writers.remove(w)

        # 키포인트 전송
        dead = [w for w in keypoint_writers
                if not await _try_send_json(w, keypoint_data)]
        for w in dead: keypoint_writers.remove(w)

        frame_idx += 1
        await asyncio.sleep(1/30)

    cap.release()

async def _try_send_jpeg(writer, frame) -> bool:
    try:
        await send_jpeg(writer, frame)
        return True
    except:
        return False

async def _try_send_json(writer, obj) -> bool:
    try:
        await send_json(writer, obj)
        return True
    except:
        return False

# ── 서버 시작 ─────────────────────────────────────────────────
async def main():
    frame_server    = await asyncio.start_server(
        handle_frame_client,    "127.0.0.1", FRAME_PORT)
    keypoint_server = await asyncio.start_server(
        handle_keypoint_client, "127.0.0.1", KEYPOINT_PORT)
    control_server  = await asyncio.start_server(
        handle_control_client,  "127.0.0.1", CONTROL_PORT)

    print(f"[Server] 프레임 포트:   127.0.0.1:{FRAME_PORT}")
    print(f"[Server] 키포인트 포트: 127.0.0.1:{KEYPOINT_PORT}")
    print(f"[Server] 제어 포트:     127.0.0.1:{CONTROL_PORT}")

    async with frame_server, keypoint_server, control_server:
        await asyncio.gather(
            frame_server.serve_forever(),
            keypoint_server.serve_forever(),
            control_server.serve_forever(),
            capture_loop()
        )

if __name__ == "__main__":
    asyncio.run(main())
