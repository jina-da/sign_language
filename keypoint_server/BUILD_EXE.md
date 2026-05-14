# keypoint_server.exe 빌드 가이드

배포용 실행 파일을 생성하는 방법입니다.  
Python 없이도 `keypoint_server.exe` 단독으로 실행할 수 있습니다.

## 1. 준비

```bash
pip install pyinstaller
```

## 2. 빌드

`keypoint_server/` 폴더에서 실행합니다.

```bash
pyinstaller --onefile --noconsole ^
    --add-data "hand_landmarker.task;." ^
    --add-data "pose_landmarker_lite.task;." ^
    keypoint_server.py
```

> **주의:** 모델 파일(`.task`)이 없으면 첫 실행 시 자동 다운로드됩니다.  
> 다운로드 후 빌드하면 모델이 exe 안에 포함됩니다.

## 3. 배포 구조

빌드 후 `dist/keypoint_server.exe` 를 아래 위치에 복사합니다.

```
SignLearnClient.exe
keypoint_server/
    keypoint_server.exe   ← 여기에 복사
    hand_landmarker.task
    pose_landmarker_lite.task
```

## 4. 동작 확인

Qt 앱 실행 시 디버그 콘솔에 아래 메시지가 나오면 정상입니다.

```
[KP] keypoint_server.exe 시작 (PID: XXXX)
[KP] keypoint_server 연결 성공
```

## 참고: 개발 환경에서는 bat 파일 사용

`keypoint_server.exe` 가 없으면 자동으로 `run_keypoint_server.bat` 을 실행합니다.  
개발 중에는 별도 빌드 없이 기존 방식 그대로 동작합니다.
