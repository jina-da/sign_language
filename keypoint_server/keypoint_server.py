"""
keypoint_server.py — MediaPipe 0.10.x Tasks API 수정 버전
"""

import asyncio
import json
import struct
import sys
import threading
import time
import queue as _queue   # 스레드 안전
import cv2
import mediapipe as mp
from mediapipe.tasks import python as mp_python
from mediapipe.tasks.python.vision import HandLandmarker, HandLandmarkerOptions
from mediapipe.tasks.python.vision import PoseLandmarker, PoseLandmarkerOptions
from mediapipe.tasks.python.vision.core.vision_task_running_mode import VisionTaskRunningMode
import urllib.request
import os

# stdout 버퍼링 비활성화 — Qt 콘솔에 로그가 즉시 표시되도록
sys.stdout.reconfigure(encoding='utf-8', line_buffering=True)

# ── 설정 ──────────────────────────────────────────────────────
FRAME_PORT    = 7000
KEYPOINT_PORT = 7001
CONTROL_PORT  = 7002   # Qt → Python 제어 메시지 (우세손 설정 등)
CAMERA_INDEX  = 0
FRAME_WIDTH    = 1920   # 캡처 해상도 (키포인트 추출용)
FRAME_HEIGHT   = 1080
DISPLAY_WIDTH  = 640    # Qt 화면 전송용 (대역폭 절약)
DISPLAY_HEIGHT = 360
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
MAX_WRITE_BUFFER = 2 * 1024 * 1024  # 2MB — 초과 시 프레임 건너뜀

async def send_packet(writer, data: bytes):
    header = struct.pack(">I", len(data))
    writer.write(header + data)
    # drain() 제거: Qt가 느리게 읽을 때 예외 발생으로 연결이 끊기는 문제 방지
    # write()는 내부 버퍼에 쌓기만 하므로 예외가 발생하지 않음

async def send_json(writer, obj: dict):
    await send_packet(writer, json.dumps(obj).encode("utf-8"))

async def send_jpeg(writer, frame_bgr):
    # 버퍼가 가득 찼으면 이 프레임은 건너뜀 (연결은 유지)
    if writer.transport and writer.transport.get_write_buffer_size() > MAX_WRITE_BUFFER:
        return
    _, jpeg = cv2.imencode(".jpg", frame_bgr,
                           [cv2.IMWRITE_JPEG_QUALITY, JPEG_QUALITY])
    await send_packet(writer, jpeg.tobytes())

async def _try_send_jpeg(writer, frame) -> bool:
    try:
        await send_jpeg(writer, frame)
        return True
    except Exception:
        return False

async def _try_send_json(writer, obj) -> bool:
    try:
        await send_json(writer, obj)
        return True
    except Exception:
        return False

# ── 관절 추출 헬퍼 ───────────────────────────────────────────
def extract_hand(hand_landmarks, handedness_list, label, img_width, img_height):
    for i, h in enumerate(handedness_list):
        # display_name 대신 category_name 사용
        if h[0].category_name == label:
            return [[lm.x * img_width, lm.y * img_height, lm.z] for lm in hand_landmarks[i]]
    return None  # 감지 안 됨을 None으로 구분

def hand_detected(hand_data):
    """손 데이터가 실제로 감지됐는지 확인"""
    return hand_data is not None

def hand_to_list(hand_data):
    """None이면 zeros 반환"""
    return hand_data if hand_data is not None else [[0.0, 0.0, 0.0]] * 21

def extract_pose(pose_landmarks, img_width, img_height):
    if not pose_landmarks:
        return [[0.0, 0.0, 0.0]] * 25
    return [[lm.x * img_width, lm.y * img_height, lm.visibility] for lm in pose_landmarks[0][:25]]

# ── 우세손 정규화 ─────────────────────────────────────────────
def normalize_dominant_hand(left_hand, right_hand, img_width):
    """
    왼손잡이일 경우: x좌표 반전(img_width - x) + 왼손↔오른손 채널 swap
    오른손잡이: 그대로 반환
    """
    if not is_dominant_left:
        return left_hand, right_hand

    # x좌표 반전 (픽셀 기준)
    def flip_x(hand):
        return [[img_width - lm[0], lm[1], lm[2]] for lm in hand]

    # 채널 swap: 왼손 ↔ 오른손
    return flip_x(right_hand), flip_x(left_hand)

# ── 공수 자세 감지 ───────────────────────────────────────────
def is_gongsu_pose(left_hand, right_hand, pose_lms):
    """
    공수 자세: 양손이 몸 중앙에 가까이 모인 상태
    조건:
      1. 양손 모두 감지됨
      2. 두 손 wrist 간 거리가 가까움 (픽셀 기준 160px 이하, 640px 폭 기준 25%)
      3. 손 중앙이 어깨 중앙에서 너무 멀지 않음 (픽셀 기준 192px 이하, 480px 높이 기준 40%)
    """
    if not hand_detected(left_hand) or not hand_detected(right_hand):
        return False

    lx, ly = left_hand[0][0],  left_hand[0][1]
    rx, ry = right_hand[0][0], right_hand[0][1]

    hand_dist = ((lx - rx) ** 2 + (ly - ry) ** 2) ** 0.5
    if hand_dist > 480:   # 0.25 * 1920
        return False

    cx = (lx + rx) / 2
    cy = (ly + ry) / 2
    if len(pose_lms) > 12:
        sx = (pose_lms[11][0] + pose_lms[12][0]) / 2
        sy = (pose_lms[11][1] + pose_lms[12][1]) / 2
        body_dist = ((cx - sx) ** 2 + (cy - sy) ** 2) ** 0.5
        if body_dist > 432:   # 0.4 * 1080
            return False

    return True

# ── 클라이언트 관리 ───────────────────────────────────────────
frame_writers    = []
keypoint_writers = []

async def _wait_until_disconnected(reader):
    """
    클라이언트가 끊길 때까지 대기한다.
    Qt 클라이언트는 데이터를 보내지 않으므로 EOF(빈 bytes)로 종료를 감지한다.
    """
    try:
        while True:
            data = await reader.read(1024)
            if not data:   # EOF — 상대방이 연결을 정상 종료
                break
    except Exception:
        pass   # 강제 종료(abort) 등 예외도 동일하게 처리

async def handle_frame_client(reader, writer):
    addr = writer.get_extra_info("peername")
    print(f"[Frame] Qt 연결: {addr}")
    frame_writers.append(writer)
    try:
        await _wait_until_disconnected(reader)
    finally:
        if writer in frame_writers:
            frame_writers.remove(writer)
        print(f"[Frame] Qt 연결 종료: {addr}")

async def handle_keypoint_client(reader, writer):
    addr = writer.get_extra_info("peername")
    print(f"[Keypoint] Qt 연결: {addr}")
    keypoint_writers.append(writer)
    try:
        await _wait_until_disconnected(reader)
    finally:
        if writer in keypoint_writers:
            keypoint_writers.remove(writer)
        print(f"[Keypoint] Qt 연결 종료: {addr}")

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
# 원본 구조(단일 asyncio 루프)를 유지한다.
# cap.read()는 33ms 내에 반환되므로 이벤트 루프 블로킹이 허용 범위 안에 있다.
# 루프 끝에서 await asyncio.sleep(0)으로 매 프레임마다 이벤트 루프에
# 제어권을 즉시 양보하여 TCP accept/send가 처리될 수 있도록 한다.
async def capture_loop():
    # 카메라 열기 — 이전 프로세스가 점유 중일 수 있으므로 최대 10회 재시도
    cap = None
    for attempt in range(10):
        cap = cv2.VideoCapture(CAMERA_INDEX)
        if cap.isOpened():
            break
        print(f"[Camera] 열기 실패 ({attempt + 1}/10) — 2초 후 재시도...")
        cap.release()
        await asyncio.sleep(2.0)

    if cap is None or not cap.isOpened():
        print("[Camera] 카메라를 열 수 없습니다. 카메라 연결 상태를 확인하세요.")
        return

    cap.set(cv2.CAP_PROP_FRAME_WIDTH,  FRAME_WIDTH)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, FRAME_HEIGHT)
    img_width  = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    img_height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    print(f"[Camera] 캡처 시작 — 해상도: {img_width}x{img_height}")

    frame_idx    = 0
    timestamp_ms = 0
    _prev_debug  = None
    fail_count   = 0
    FAIL_REOPEN  = 60   # 이 횟수 이상 실패 시 카메라 재오픈 시도

    while True:
        ret, frame = cap.read()
        if not ret:
            fail_count += 1
            if fail_count <= 5:
                print(f"[Camera] 프레임 읽기 실패 ({fail_count}회)")
            elif fail_count == 6:
                print("[Camera] 반복 실패 — 이후 로그 생략, 재시도 중...")

            # 일정 횟수 이상 실패 시 카메라 재오픈 시도 (연결 해제 후 재연결 대응)
            if fail_count >= FAIL_REOPEN:
                print(f"[Camera] {FAIL_REOPEN}회 실패 — 카메라 재오픈 시도...")
                cap.release()
                await asyncio.sleep(2.0)
                cap = cv2.VideoCapture(CAMERA_INDEX)
                if cap.isOpened():
                    cap.set(cv2.CAP_PROP_FRAME_WIDTH,  FRAME_WIDTH)
                    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, FRAME_HEIGHT)
                    img_width  = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
                    img_height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
                    print(f"[Camera] 재오픈 성공 — 해상도: {img_width}x{img_height}")
                    fail_count = 0
                else:
                    print("[Camera] 재오픈 실패 — 계속 대기...")
                    fail_count = FAIL_REOPEN   # 카운터 유지해서 계속 재시도
            else:
                await asyncio.sleep(0.01)
            continue

        if fail_count > 0:
            print(f"[Camera] 프레임 복구 (실패 {fail_count}회 후)")
            fail_count = 0

        timestamp_ms += 33

        rgb      = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        mp_image = mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb)

        hand_result = hand_landmarker.detect_for_video(mp_image, timestamp_ms)
        pose_result = pose_landmarker.detect_for_video(mp_image, timestamp_ms)

        left_hand_raw  = extract_hand(hand_result.hand_landmarks,
                                      hand_result.handedness, "Left",
                                      img_width, img_height)
        right_hand_raw = extract_hand(hand_result.hand_landmarks,
                                      hand_result.handedness, "Right",
                                      img_width, img_height)

        left_normalized, right_normalized = normalize_dominant_hand(
            hand_to_list(left_hand_raw),
            hand_to_list(right_hand_raw),
            img_width
        )

        left_hand  = left_normalized
        right_hand = right_normalized
        pose_lms   = extract_pose(pose_result.pose_landmarks, img_width, img_height)

        gongsu = is_gongsu_pose(left_hand_raw, right_hand_raw, pose_lms)

        # 디버그 출력 — 상태 변화 시에만 출력
        num_hands      = len(hand_result.hand_landmarks) if hand_result.hand_landmarks else 0
        left_detected  = hand_detected(left_hand_raw)
        right_detected = hand_detected(right_hand_raw)
        debug_state    = (num_hands, left_detected, right_detected, gongsu)
        if debug_state != _prev_debug:
            print(f"[Debug] 감지된 손 수: {num_hands}")
            print(f"[Debug] 왼손:{left_detected} 오른손:{right_detected} 공수:{gongsu}")
            if num_hands > 0 and hand_result.handedness:
                for i, h in enumerate(hand_result.handedness):
                    print(f"  손{i}: category_name={h[0].category_name} score={h[0].score:.2f}")
            _prev_debug = debug_state

        keypoint_data = {
            "frame_idx":  frame_idx,
            "left_hand":  left_hand,
            "right_hand": right_hand,
            "pose":       pose_lms,
            "is_gongsu":  gongsu
        }

        # 프레임 전송 (640x360으로 리사이즈하여 대역폭 절약)
        display_frame = cv2.resize(frame, (DISPLAY_WIDTH, DISPLAY_HEIGHT),
                                   interpolation=cv2.INTER_LINEAR)
        dead = [w for w in frame_writers
                if not await _try_send_jpeg(w, display_frame)]
        for w in dead: frame_writers.remove(w)

        # 키포인트 전송
        dead = [w for w in keypoint_writers
                if not await _try_send_json(w, keypoint_data)]
        for w in dead: keypoint_writers.remove(w)

        frame_idx += 1
        # 매 프레임 후 이벤트 루프에 제어권 즉시 양보
        await asyncio.sleep(0)

    cap.release()


# ── 서버 시작 ─────────────────────────────────────────────────
async def main():
    # reuse_address=True: 이전 프로세스가 포트를 점유 중이어도 즉시 바인딩
    frame_server    = await asyncio.start_server(
        handle_frame_client,    "127.0.0.1", FRAME_PORT,    reuse_address=True)
    keypoint_server = await asyncio.start_server(
        handle_keypoint_client, "127.0.0.1", KEYPOINT_PORT, reuse_address=True)
    control_server  = await asyncio.start_server(
        handle_control_client,  "127.0.0.1", CONTROL_PORT,  reuse_address=True)

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
