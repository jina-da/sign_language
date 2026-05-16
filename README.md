# SignLearn — 한국 수화 학습 클라이언트

AI 머신비전 기반 한국 수화 학습 시스템의 Qt/C++ 클라이언트입니다.  
카메라로 사용자의 수화 동작을 실시간 인식하고 정확도 피드백을 제공합니다.  
수화를 배우려는 청인뿐만 아니라, 수화는 알지만 한글 단어가 낯선 농인을 위한 역방향 사전 기능도 포함합니다.

---

## 주요 기능

| 기능 | 설명 |
|---|---|
| **학습 모드** | 수화 영상 시청 후 따라하면 AI가 정확도를 평가 |
| **복습 모드** | 오답·미완료 단어를 모아 재학습 |
| **테스트 모드** | 학습 완료 후 수화 입력으로 단어 맞추기 |
| **사전 (정방향)** | 단어 검색 → 수화 영상 확인 |
| **사전 (역방향)** | 수화 입력 → 단어 검색 (농인용) |
| **개인 설정** | 우세손, 일일 학습 목표, 키포인트 동의 등 |

---

## 시스템 구조

```
사용자 PC
├── SignLearnClient.exe     Qt/C++ 클라이언트
│   ├── AppController       메시지 파싱 및 화면 전환 총괄
│   ├── TcpClient           운용서버 TCP 통신 (PORT 9000)
│   ├── KeypointClient      keypoint_server와 로컬 TCP 통신
│   └── 각종 Widget         화면 구성
│
└── keypoint_server(.exe)   Python / MediaPipe 로컬 서버
    ├── PORT 7000           카메라 프레임 전송 (→ Qt)
    ├── PORT 7001           키포인트 JSON 전송 (→ Qt)
    └── PORT 7002           제어 메시지 수신 (Qt →)

외부 서버 (10.10.10.114)
├── PORT 9000   운용서버 (TCP/JSON) — 로그인·학습·추론·설정
└── PORT 8000   영상서버 (FastAPI)  — 수화 영상 스트리밍
```

---

## 아키텍처

```
[카메라]
    │ OpenCV 프레임
    ▼
[keypoint_server.exe]  ←── 제어 메시지 (PORT 7002)
    │ 프레임 (PORT 7000)
    │ 키포인트 JSON (PORT 7001)
    ▼
[SignLearnClient.exe]
    │ TCP/JSON + 4B 헤더 (PORT 9000)
    ▼
[운용서버 (10.10.10.114)]


[SignLearnClient.exe]
    │ HTTP 스트리밍 (PORT 8000)
    ▼
[영상서버 (10.10.10.114)]  ── 수화 영상 재생
```

---

## 동작 흐름

```
1. 로그인
   REQ_LOGIN → RES_LOGIN
   홈 화면 표시 (진도바, 복습 대기 수, 최고점수 반영)

2. 학습 모드
   REQ_DAILY_WORDS → RES_DAILY_WORDS
   수화 영상 재생 → 사용자 수화 입력
   REQ_INFER → RES_INFER (정확도 피드백)
   전체 단어 완료 시 테스트 모드로 전환 가능

      ↓

   테스트 모드
   학습한 단어 목록으로 수화 입력 → 단어 맞추기
   REQ_INFER → RES_INFER
   테스트 완료 → 홈으로 복귀

3. 복습 모드
   REQ_REVIEW_WORDS → RES_REVIEW_WORDS
   오답·미완료 단어 재학습
   REQ_INFER → RES_INFER
   복습 완료 → 홈으로 복귀

4. 사전
   정방향: 단어 입력 → REQ_DICT_SEARCH → RES_DICT_SEARCH → 수화 영상 재생
   역방향: 수화 입력 → REQ_DICT_REVERSE → RES_DICT_REVERSE → 단어 표시

5. 개인 설정
   하루 목표 단어 수, 우세손, 농인 여부, 키포인트 수집 동의
   비밀번호 변경
```

---

## 주요 동작 수치

| 항목 | 값 |
|---|---|
| 운용서버 자동 재연결 간격 | 3초 |
| keypoint_server 재연결 간격 | 3초 |
| 녹화 시작 카운트다운 | 3초 |
| 움직임 정지 감지 후 자동 녹화 종료 | 1.5초 |
| 움직임 감지 임계값 | 19.2px (1920×1080 기준 약 1%) |
| 서버 전송 최소 프레임 수 | 3프레임 미만 시 전송 취소 |
| keypoint_server 캡처 해상도 | 1920×1080 |
| Qt 전송 프레임 해상도 | 640×360 (JPEG 품질 70) |

---

## 정답 판정 기준

`RES_INFER` 응답의 `result`와 `accuracy` 조합으로 3단계 판정합니다.

| 판정 | 조건 |
|---|---|
| 정답 | `result: true` AND `accuracy ≥ 0.8` |
| 다시 시도 | `result: true` AND `accuracy < 0.8` |
| 오답 | `result: false` |

---

## 수화 영상 캐시

수화 영상은 최초 재생 시 `videos/` 폴더에 로컬 캐시로 저장됩니다.  
이후 동일 영상은 서버 요청 없이 캐시에서 즉시 재생합니다.

---

## 주요 기능 상세

### 학습 모드

오늘의 단어 목록을 수신해 단어별로 수화 영상 시청 → 수화 입력 → AI 판정 순서로 진행합니다.

```
REQ_DAILY_WORDS → RES_DAILY_WORDS (단어 목록 수신)
    │
    ▼
수화 영상 재생 (속도 0.25x ~ 2x 조절 가능)
    │
    ▼
녹화 버튼 클릭 (또는 스페이스바) → 3초 카운트다운 → 녹화 시작
    │
    ▼
손 움직임 감지 중 — 1.5초 정지 시 자동 종료
    │
    ▼
REQ_INFER (keypoint frames 전송) → RES_INFER (정답 판정 수신)
    │
    ├─ 정답 (result: true, accuracy ≥ 0.8)  →  ○ 정답!
    ├─ 재시도 (result: true, accuracy < 0.8) →  △ 다시 시도
    └─ 오답 (result: false)                  →  ✗ 오답입니다
    │
    ▼
전체 단어 완료 → 테스트 모드 전환 가능
```

### 테스트 모드

학습 완료한 단어 목록으로 수화 입력 → 단어 맞추기 형식으로 진행합니다.  
학습 모드와 동일한 녹화·판정 흐름을 사용하며, 별도 영상 재생 없이 단어만 제시합니다.

### 복습 모드

서버가 오답·미완료 단어를 선별해 `RES_REVIEW_WORDS`로 전달합니다.  
클라이언트는 수신한 목록을 그대로 사용하며, 복습 조건 판단은 서버가 담당합니다.

### 사전

| 방향 | 흐름 |
|---|---|
| 정방향 | 한글 단어 입력 → REQ_DICT_SEARCH → 수화 영상 재생 |
| 역방향 | 수화 입력 → REQ_DICT_REVERSE → 단어·뜻 표시 (농인 대상) |

### 수화 입력 (REQ_INFER 전송 구조)

```json
{
  "type": "REQ_INFER",
  "session_token": "...",
  "word_id": 50,
  "keypoint_version": "v1",
  "total_frames": 23,
  "frames": [ { "frame_idx": 0, "left_hand": [...], "right_hand": [...], "pose": [...] }, ... ]
}
```

3프레임 미만 수집 시 전송을 취소하고 재입력을 안내합니다.

### 개인 설정

| 항목 | 설명 |
|---|---|
| 하루 목표 단어 수 | 5 ~ 30개, 홈 화면 진도바에 반영 |
| 우세손 | 오른손 / 왼손 — keypoint_server의 좌우 반전 정규화에 사용 |
| 농인 여부 | 회원가입 시 선택, 역방향 사전 기능 활성화 |
| 키포인트 수집 동의 | 동의 시 추론 데이터가 서버 재학습에 활용됨 |
| 비밀번호 변경 | 현재 비밀번호 확인 후 변경 |
| 회원 탈퇴 | 비밀번호 확인 후 처리 |

---

## 핵심 설계 포인트

### AppController 중앙 집중 라우팅

모든 서버 메시지 수신과 화면 전환은 `AppController` 한 곳에서 처리합니다.  
각 Widget은 사용자 입력 시그널만 emit하고, AppController가 이를 수신해 서버 송신·화면 전환·상태 관리를 담당합니다. Widget 간 직접 참조가 없어 의존성이 단방향으로 유지됩니다.

### 세션 만료 자동 처리

서버로부터 `RES_ERROR (error_code: 1003)` 수신 시 세션 만료로 간주합니다.  
세션 토큰을 즉시 초기화하고 로그인 화면으로 강제 전환하며, 사용자에게 재로그인 안내 메시지를 표시합니다.

### keypoint_server 자동 실행 및 fallback

Qt 앱 시작 시 `keypoint_server.exe` → `run_keypoint_server.bat` → `keypoint_server.py` 순으로 자동 실행을 시도합니다.  
실행 전 포트 7000 점유 프로세스를 자동 정리하고, `reuse_address=True`로 바인딩해 이전 프로세스 종료 직후에도 즉시 재기동됩니다.

### 키포인트 추론 파이프라인

```
카메라 프레임 (1920×1080)
    │ keypoint_server에서 추출
    ▼
pose 25관절 + left_hand 21관절 + right_hand 21관절 (픽셀 좌표 + z/visibility)
    │ 우세손 정규화 (왼손잡이: x좌표 반전 + 좌우 채널 swap)
    │ Qt로 JSON 전송 (640×360 프레임과 동기화)
    ▼
녹화 중 버퍼에 누적 → 1.5초 정지 감지 시 자동 종료
    │ 3프레임 미만이면 전송 취소
    ▼
REQ_INFER로 운용서버 전송
```

### 수화 영상 캐시 전략

첫 재생 시 영상서버에서 스트리밍하는 동시에 `videos/` 폴더에 백그라운드 다운로드합니다.  
이후 동일 영상은 로컬 캐시에서 즉시 재생해 서버 부하를 줄입니다.

### 홈 화면 데이터 동기화

홈 탭 진입 시마다 `REQ_DAILY_WORDS`를 재전송해 진도·복습 대기 수·오늘 학습 단어 목록을 항상 서버 최신값으로 갱신합니다.  
학습·복습 중 서버 DB는 `REQ_INFER` 처리 시마다 즉시 갱신되므로, 홈에 돌아왔을 때 정확한 수치가 표시됩니다.

---

## 개발 환경

| 항목 | 내용 |
|---|---|
| OS | Windows 10 / 11 |
| Qt | 6.11.0 MinGW 64bit |
| C++ | C++17 |
| OpenCV | MinGW 빌드 (`C:/opencv_mingw`) |
| Python | 3.x (keypoint_server용) |
| MediaPipe | `mediapipe` 패키지 |

---

## 빌드 및 실행

### 1. Qt 클라이언트

Qt Creator에서 `CMakeLists.txt`를 열고 빌드합니다.

OpenCV 경로가 다른 경우 `CMakeLists.txt`의 아래 줄을 수정합니다.

```cmake
set(OpenCV_DIR "C:/opencv_mingw/x64/mingw/lib")
```

### 2. 서버 IP 설정

`AppController.h`의 상수를 실제 서버 IP로 변경합니다.

```cpp
static constexpr const char* SERVER_HOST = "10.10.10.114";
static constexpr int         SERVER_PORT = 9000;
```

### 3. keypoint_server 실행 (개발 환경)

`keypoint_server/` 폴더 안에 `run_keypoint_server.bat`을 만들고 아래 내용을 작성합니다.

```bat
@echo off
C:\Python3x\python.exe keypoint_server.py
```

Qt 앱 실행 시 `keypoint_server.exe`가 없으면 자동으로 bat 파일을 실행합니다.

### 4. Python 의존 패키지 설치

```bash
pip install mediapipe opencv-python numpy
```

---

## 배포

`keypoint_server.py`를 PyInstaller로 단일 exe로 패키징하면 Python 없이 배포할 수 있습니다.  
→ `keypoint_server/BUILD_GUIDE.txt` 참고

Qt 클라이언트 배포 폴더 구조는 다음과 같습니다.

```
배포 폴더/
├── SignLearnClient.exe
├── keypoint_server/
│   ├── keypoint_server.exe
│   ├── hand_landmarker.task
│   └── pose_landmarker_lite.task
└── (windeployqt 수집 DLL 및 플러그인)
```

---

## 파일 구조

```
SignLanguage_Client/
├── main.cpp
├── AppController.h / .cpp      전체 상태·메시지 관리
├── camera/
│   ├── KeypointClient.h / .cpp keypoint_server 소켓 연결 및 프레임 수신
├── network/
│   ├── TcpClient.h / .cpp      운용서버 TCP 송수신 (자동 재연결 포함)
│   └── ProtocolHandler.h / .cpp JSON 직렬화·역직렬화
├── keypoint_server/
│   ├── keypoint_server.py      MediaPipe 키포인트 추출 로컬 서버
│   └── BUILD_GUIDE.txt         PyInstaller 빌드 가이드
└── widgets/
    ├── AppStyle.h              전역 색상·스타일 상수
    ├── VideoPlayer.h / .cpp    수화 영상 스트리밍 + 로컬 캐시 재생
    ├── MainWindow.h / .cpp     네비게이션 및 화면 전환
    ├── LoginWidget             로그인
    ├── RegisterWidget          회원가입
    ├── StudyWidget             학습 모드
    ├── ReviewWidget            복습 모드 (StudyWidget.ui 재사용)
    ├── TestWidget              테스트 모드
    ├── DictWidget              수화 사전 (정방향 + 역방향)
    └── SettingsWidget          개인 설정
```

---

## 통신 프로토콜 요약

| type | 번호 | 설명 |
|---|---|---|
| REQ_LOGIN / RES_LOGIN | 101 / 102 | 로그인 |
| REQ_LOGOUT / RES_LOGOUT | 103 / 104 | 로그아웃 |
| REQ_REGISTER / RES_REGISTER | 105 / 106 | 회원가입 |
| REQ_DAILY_WORDS / RES_DAILY_WORDS | 201 / 202 | 오늘의 학습 단어 |
| REQ_INFER / RES_INFER | 203 / 204 | 수화 키포인트 추론 요청·결과 |
| REQ_REVIEW_WORDS / RES_REVIEW_WORDS | 205 / 206 | 복습 단어 목록 |
| REQ_DICT_SEARCH / RES_DICT_SEARCH | 301 / 302 | 사전 정방향 검색 |
| REQ_DICT_REVERSE / RES_DICT_REVERSE | 303 / 304 | 사전 역방향 검색 |
| REQ_SETTINGS_* / RES_SETTINGS_* | 801~812 | 설정 변경 |
| RES_ERROR | 703 | 서버 에러 응답 |

메시지 형식은 모두 JSON이며 4바이트 빅엔디언 길이 헤더를 앞에 붙입니다.

```json
{ "type": "REQ_LOGIN", "username": "user", "password": "****" }
```
