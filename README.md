# 🤟 SignLearn — 한국 수화 학습 시스템

![Python](https://img.shields.io/badge/Python-3.12~3.13-3776AB?style=flat-square&logo=python&logoColor=white)
![PyTorch](https://img.shields.io/badge/PyTorch-2.11.0-EE4C2C?style=flat-square&logo=pytorch&logoColor=white)
![Qt](https://img.shields.io/badge/Qt-6.11.0-41CD52?style=flat-square&logo=qt&logoColor=white)
![MariaDB](https://img.shields.io/badge/MariaDB-10.x-003545?style=flat-square&logo=mariadb&logoColor=white)
![CUDA](https://img.shields.io/badge/CUDA-13.0-76B900?style=flat-square&logo=nvidia&logoColor=white)

> 카메라로 사용자의 수화 동작을 실시간으로 인식하고 정확도 피드백을 제공하는  
> **AI 머신비전 기반 한국 수화 교육 프로그램**  
> 청인의 수화 학습뿐만 아니라, 수화는 알지만 한글 단어가 낯선 **농인을 위한 역방향 사전**도 포함한 양방향 서비스

---

## 👥 팀 구성

| 이름 | 역할 | 브랜치 |
|------|------|--------|
| 이지나 (팀장) | AI 서버 설계 및 구현 | [`ai`](../../tree/ai) |
| 정지훈 | 운용 서버 + DB 설계 및 구현 | [`server`](../../tree/server) |
| 김희창 | 클라이언트 GUI 구현 | [`client`](../../tree/client) |

---

## 🏗️ 시스템 아키텍처

```
[C++ Qt 클라이언트]
        │
        │ ① 로그인 / 학습 / 추론 / 사전 / 게임 (TCP + JSON)
        ▼
[운용 서버 :9000]  ──────────────── MariaDB :3306
        │                                  ▲
        │ ② 수화 keypoint 추론 요청 (TCP)  │ ④ 세션 검증 (DB 공유)
        ▼                                  │
[AI 서버 :9100]              [FastAPI 동영상 서버 :8000]
  (RTX 5090 GPU)                           │
                                           │ ③ mp4 스트리밍 (HTTP Range)
                                           ▼
                              [C++ Qt 클라이언트]
```

| 구간 | 프로토콜 | 설명 |
|------|---------|------|
| 클라이언트 ↔ 운용서버 | TCP + JSON (4-byte 헤더) | 학습·추론·사전·게임 전 기능 |
| 운용서버 ↔ AI서버 | TCP + JSON (동일 프로토콜) | 수화 keypoint 추론, 재학습 |
| 클라이언트 ↔ 동영상서버 | HTTP Range (206) | mp4 스트리밍, seek 지원 |
| 동영상서버 ↔ DB | aiomysql | 세션 유효성 검증 (운용서버와 DB 공유) |

---

## 🖥️ 개발 환경

| 항목 | 기술 |
|------|------|
| 클라이언트 | C++ / Qt 6.11.0 / Windows |
| 운용 서버 | Python 3.12 / asyncio / aiomysql |
| 동영상 서버 | Python / FastAPI / uvicorn |
| AI 서버 | Python 3.13 / PyTorch 2.11.0 / CUDA 13.0 (RTX 5090) |
| DB | MariaDB |
| 데이터셋 | AI Hub 한국 수어 영상 데이터셋 (1,000단어) |

---

## 📦 브랜치별 구성

### 🤖 AI 서버 — [`ai` 브랜치](../../tree/ai)

한국 수어 keypoint 시퀀스를 입력받아 1,000개 단어를 실시간 분류하는 추론 서버.

| 항목 | 내용 |
|------|------|
| 모델 | Bidirectional GRU (hidden_dim=512, layers=3) |
| 입력 | 좌표 134차원 + 차분 134차원 = **268차원** |
| 데이터 | 15,837개 샘플 → 증강 후 63,345개 |
| 정확도 | **test_acc 93.69%** |

**성능 개선 과정:**

| 버전 | 주요 변경 | test_acc |
|------|----------|---------|
| v1~v5 | 증강, 하이퍼파라미터 튜닝 | 79.42% → 91.48% |
| v6 | OpenPose→MediaPipe 관절 순서 매핑 | 93.18% |
| **v7** | 차분(delta) 추가 | **93.69%** |

> 자세한 내용: [ai_server/README.md](ai_server/README.md)

---

### 🖧 운용 서버 — [`server` 브랜치](../../tree/server)

클라이언트와 AI 서버 사이의 핵심 백엔드. 연결 수락·라우팅·DB 저장·재학습 스케줄링 담당.

**핵심 설계:**
- 응답 우선, 저장 후순위 (`asyncio.create_task()` 비동기 처리)
- AI 연결 풀 (`asyncio.Queue` 기반 TCP 연결 재사용)
- 동영상 서버와 DB 직접 공유로 세션 검증

---

### 💻 클라이언트 — [`client` 브랜치](../../tree/client)

C++ Qt 기반 GUI. MediaPipe keypoint 추출 후 운용서버로 전송.

**주요 화면:** 로그인 / 회원가입 / 오늘의 단어 / 복습 / 테스트 / 사전 / 개인설정

---

## 🔑 주요 기능

### 수화 추론 파이프라인

```
클라이언트 keypoint frames
(pose 25 + left_hand 21 + right_hand 21 관절, 268차원)
    │
    ▼
운용서버 → AI서버 REQ_AI_INFER
    │
    ▼
AI서버 → predicted_word_id + confidence 반환
    │
    ├─ confidence ≥ 0.7  →  ○ 정답
    ├─ confidence ≥ 0.5  →  △ 다시 시도 + 재학습 후보 저장
    └─ confidence < 0.5  →  ✗ 오답
    │
    ▼ (클라이언트에 즉시 응답 후 비동기 DB 저장)
user_progress / user_history / keypoint_store
```

### 자동 재학습 파이프라인

```
매일 새벽 3시 또는 keypoint 1,000개 초과 시
    ↓
keypoint_store 미학습 데이터 로드
    ↓
신뢰도 필터링 (confidence < 0.5 버림)
    ↓
Fine-tuning (epoch=10, lr=1e-5)
    ↓
성능 향상 → 자동 배포 / 하락 → 자동 롤백
```

---

## 🗄️ DB 주요 테이블

| 테이블 | 설명 |
|--------|------|
| `users` | 사용자 계정 (우세손, 농인 여부, keypoint 동의 등) |
| `user_session` | 세션 토큰, 시작/종료 시각 |
| `word_info` | 단어 목록 (1,000개, 난이도·영상경로·word_number) |
| `user_progress` | 사용자별 단어 정확도·시도횟수 누적 |
| `user_history` | 추론 이력 JSON |
| `keypoint_store` | 재학습용 keypoint (134차원 플랫 배열) |
| `word_stats` | 단어별 전체 평균 정확도 통계 |
| `game_history` | 게임 이력 (점수, 소요시간, 결과 JSON) |
| `model_versions` | AI 모델 버전 관리 |

---

## 📡 통신 프로토콜

```
┌──────────────┬────────────────────────────────┐
│   4 bytes    │   N bytes                      │
│  길이 헤더    │   JSON 페이로드 (UTF-8)          │
│ (Big-Endian) │                                │
└──────────────┴────────────────────────────────┘
```

| 번호 | 타입 | 방향 | 설명 |
|------|------|------|------|
| 101/102 | REQ/RES_LOGIN | C↔S | 로그인 |
| 201/202 | REQ/RES_DAILY_WORDS | C↔S | 오늘의 단어 |
| 203/204 | REQ/RES_INFER | C↔S | 수화 추론 |
| 301/302 | REQ/RES_DICT_SEARCH | C↔S | 사전 정방향 |
| 303/304 | REQ/RES_DICT_REVERSE | C↔S | 사전 역방향 |
| 401~406 | REQ/RES_GAME_* | C↔S | 게임 |
| 501/502 | REQ/RES_AI_INFER | S↔A | AI 추론 |
| 601~607 | REQ/RES_RETRAIN_* | S↔A | 재학습·배포·롤백 |

---

## ⚙️ 실행 방법

### AI 서버
```bash
cd ai_server
source venv/bin/activate
python3 ai_server.py
```

### 운용 서버
```bash
pip install aiomysql
python ops_server_fixed.py
```

### 동영상 서버
```bash
pip install -r requirements.txt
uvicorn main:app --host 0.0.0.0 --port 8000
```

### 클라이언트
Qt Creator에서 빌드 후 실행. `keypoint_server.py` 자동 실행됨.

---

## 🛠️ 주요 트러블슈팅

### OpenPose vs MediaPipe 관절 순서 불일치
학습 데이터(OpenPose)와 클라이언트(MediaPipe)의 관절 인덱스 순서가 달라 오탐 발생.  
0번(Nose)만 일치하고 나머지 전부 불일치 → 매핑 테이블 적용 후 93.18% 달성.

### 차분(Delta) 특징 추가
절대 좌표만으로는 손 위치가 유사한 단어 구분 불가.  
프레임 간 변화량(차분) 추가로 동작 패턴 학습 강화 → 93.69% 달성.

### MariaDB OperationalError 1020
비동기 동시 요청에서 같은 행 접근 시 발생 → `conn.begin()` 트랜잭션으로 해결.

### MediaPipe C++ 미지원
Qt C++ 환경에서 MediaPipe 직접 사용 불가 → Python `keypoint_server.py`를 로컬 TCP로 연결하는 구조 채택.

---

## 📊 AI 모델 시각화

![Version Accuracy](ai_server/portfolio_images/01_version_accuracy.png)

![Training Curve](ai_server/portfolio_images/02_training_curve.png)

![Joint Order Mismatch](ai_server/portfolio_images/03_joint_order_mismatch.png)

![False Positive Analysis](ai_server/portfolio_images/04_fp_analysis.png)

---

## 📋 데이터셋

AI Hub 한국 수어 영상 데이터셋 사용. 원본 데이터는 저작권상 레포에 포함되지 않음.  
다운로드: [AI Hub](https://aihub.or.kr) → "한국수어 영상" 검색 후 신청

---

## 🔮 향후 개선 방향

1. REAL mp4 원천 영상 MediaPipe 재전처리 → 학습/추론 도구 통일
2. CROWD 데이터 추가로 다양한 화자 일반화
3. 3,000단어로 확장
4. 수화 문장 인식 (시퀀스 모델 심화)
5. 비수지신호 (표정) 인식 추가
6. 다국어 수화 지원 (ASL 등)
