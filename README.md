# SignLearn — 한국 수화 학습 시스템

카메라로 사용자의 수화 동작을 실시간으로 인식하고 정확도 피드백을 제공하는 AI 머신비전 기반 수화 교육 프로그램입니다.  
청인의 수화 학습뿐만 아니라, 수화는 알지만 한글 단어가 낯선 농인을 위한 역방향 사전 기능도 포함한 양방향 서비스입니다.

---

## 전체 시스템 아키텍처

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

## 개발 환경

| 항목 | 기술 |
|------|------|
| 클라이언트 | C++ / Qt / Windows |
| 운용 서버 | Python 3.12 / asyncio / aiomysql |
| 동영상 서버 | Python / FastAPI / uvicorn |
| AI 서버 | Python / GPU 서버 (RTX 5090) |
| DB | MariaDB |
| 데이터셋 | AI Hub 한국 수어 영상 데이터셋 (1,000단어) |
| 운영 환경 | Windows / WSL Ubuntu |

---

## 서버 구성

### 1. 운용 서버

클라이언트와 AI 서버 사이의 핵심 백엔드 서버입니다.  
연결 수락과 메시지 라우팅을 담당하고, 실제 처리는 도메인별 핸들러에 위임합니다.

```
server/
├── ops_server_fixed.py       # 서버 진입점, 연결 수락, 메시지 라우팅
├── protocol.py               # 통신 프로토콜 (직렬화/역직렬화, 메시지 타입)
├── db_manager_fixed.py       # MariaDB 비동기 커넥션 풀, 쿼리 메서드
├── session_manager_fixed.py  # 세션 생성·만료·정리
├── model_manager.py          # AI 모델 수신·저장·배포·롤백
├── retrain_scheduler.py      # 재학습 스케줄러 (일별 자동 실행)
└── handlers/
    ├── auth.py               # 인증 (NO.101~106)
    ├── learning.py           # 학습·추론 (NO.201~206)
    ├── dictionary.py         # 사전 (NO.301~304)
    ├── game.py               # 게임 (NO.401~406)
    ├── health.py             # 헬스체크 (NO.701~702)
    └── settings.py           # 개인설정 (NO.801~812)
```

### 2. FastAPI 동영상 서버

수화 영상 스트리밍 전담 서버입니다.  
운용 서버와 MariaDB를 공유해 별도 통신 없이 세션을 검증합니다.

```
fastapi-video-server/
├── main.py          # FastAPI 앱 진입점, CORS 설정
├── routers/
│   └── video.py     # 세션 검증 + mp4 스트리밍
└── static/videos/   # mp4 파일 (1,000개)
```

---

## 주요 기능 상세

### 인증 (NO.101~106)
- `username + SHA-256 해시` 로그인 → UUID 세션 토큰 발급
- TTL 2시간, 동시 최대 200 세션 인메모리 관리
- 로그인 응답에 우세손·농인 여부·AI 모델 버전 등 사용자 설정 포함
- 회원 탈퇴는 실제 삭제 없이 `is_active=0` Soft delete 처리

### 수화 추론 파이프라인 (NO.203~204)

```
클라이언트 keypoint frames (pose 25 + left_hand 21 + right_hand 21 관절)
    │
    ▼
운용서버 → AI 서버 REQ_AI_INFER (NO.501)
    │
    ▼
AI 서버 → predicted_word_id + confidence 반환
    │
    ├─ 예측 일치 AND confidence ≥ 0.8  →  정답
    ├─ 예측 일치 AND confidence ≥ 0.5  →  재학습 후보
    │
    ▼
클라이언트에 즉시 응답 (result, accuracy)
    │
    ▼ (비동기 처리 — 응답 지연 방지)
user_progress UPSERT (이동 평균 정확도 누적)
user_history  INSERT (추론 이력 JSON)
keypoint_store INSERT (동의 + 후보인 경우만, 공수 프레임 제거)
```

### 수화 사전 (NO.301~304)
- **정방향**: 단어 검색 → 동음이의어 전체 목록 + 수화 영상 URL 반환
- **역방향**: 수화 동작 → AI 추론 → 단어·뜻 반환 (농인 대상)

### 학습 단어 관리
- **오늘의 단어**: 미학습 단어를 난이도 오름차순으로 최대 10개
- **복습 단어**: 정확도 < 80% 또는 시도횟수 < 3인 단어를 최대 20개

### 수화 퀴즈 게임 (NO.401~406)
- 기학습 단어 중 랜덤 30개로 게임 세션 구성
- 추론 결과를 인메모리(`_active_games`)에 누적
- 게임 종료 시 총점·정답수·소요시간 집계 후 DB 저장

### 자동 재학습 파이프라인 (NO.601~607)
- 매일 새벽 3시(설정 가능) 자동 트리거
- 미학습 keypoint가 임계치(1,000개) 초과 시 AI 서버에 재학습 명령
- 재학습 완료 후 정확도 개선 시 자동 배포, 미개선 시 자동 롤백

### 동영상 스트리밍
- HTTP Range 요청(206 Partial Content)으로 Qt 미디어 플레이어 seek 지원
- 클라이언트는 첫 재생 시 스트리밍과 동시에 백그라운드 다운로드 실행
- 이후 재생은 로컬 파일 사용 → 서버 부하 감소

### 개인설정 (NO.801~812)
- 하루 목표 단어 수 / 우세손 / 농인 여부 / 비밀번호 / keypoint 수집 동의 / 회원 탈퇴

---

## 통신 프로토콜

```
┌──────────────┬────────────────────────────────┐
│   4 bytes    │   N bytes                      │
│  길이 헤더    │   JSON 페이로드 (UTF-8)          │
│ (Big-Endian) │                                │
└──────────────┴────────────────────────────────┘
```

최대 페이로드 10 MB. `REQ_PING`은 body 없이 길이=0으로 전송하며 서버가 자동 감지합니다.

### 메시지 타입 전체 목록

| 번호 | 타입 | 방향 | 설명 |
|------|------|------|------|
| 101/102 | REQ/RES_LOGIN | C↔S | 로그인 |
| 103/104 | REQ/RES_LOGOUT | C↔S | 로그아웃 |
| 105/106 | REQ/RES_REGISTER | C↔S | 회원가입 |
| 201/202 | REQ/RES_DAILY_WORDS | C↔S | 오늘의 단어 |
| 203/204 | REQ/RES_INFER | C↔S | 수화 추론 |
| 205/206 | REQ/RES_REVIEW_WORDS | C↔S | 복습 단어 |
| 301/302 | REQ/RES_DICT_SEARCH | C↔S | 사전 정방향 검색 |
| 303/304 | REQ/RES_DICT_REVERSE | C↔S | 사전 역방향 추론 |
| 401~406 | REQ/RES_GAME_* | C↔S | 게임 시작·추론·종료 |
| 501/502 | REQ/RES_AI_INFER | S↔A | AI 추론 |
| 601~607 | REQ/RES_RETRAIN_* | S↔A | 재학습·배포·롤백 |
| 701/702 | REQ_PING/RES_PONG | S↔A | 헬스체크 |
| 703 | RES_ERROR | S→C | 범용 오류 응답 |
| 801~812 | REQ/RES_SET_* | C↔S | 개인설정 |

---

## 핵심 설계 포인트

### AI 연결 풀 (AIConnectionPool)
추론 요청마다 TCP 연결을 새로 열면 지연이 발생하므로, 서버 기동 시 AI 서버와의 TCP 연결을 N개 미리 생성해 `asyncio.Queue`로 관리합니다. 연결이 끊기면 재연결을 시도하고, 실패 시 클라이언트에 오류 응답합니다.

### 응답 우선, 저장 후순위
추론 응답을 클라이언트에 먼저 반환한 뒤 DB 저장을 `asyncio.create_task()`로 비동기 처리합니다. 저장 실패 시 `asyncio.Queue` 재시도 큐에 등록하고 백그라운드 워커가 재처리합니다. 응답 지연 없이 데이터 유실을 방지합니다.

### MariaDB 동시 접근 오류 방지 (OperationalError 1020)
비동기 동시 요청에서 같은 행에 접근 시 발생하는 오류를 `conn.begin()`으로 트랜잭션을 명시적으로 시작해 방지합니다.

### 단어 캐시
`word_info` 테이블 1,000개 단어를 서버 메모리에 캐시(TTL 1시간)합니다. 추론 로그·통계 계산 시 DB 추가 조회 없이 단어명·번호를 즉시 참조합니다.

### 동영상 서버 세션 공유 인증
동영상 서버는 운용 서버와 직접 통신하지 않습니다. 두 서버가 같은 MariaDB를 공유하고 `user_session.ended_at IS NULL` 조건으로 세션을 검증합니다. 로그아웃 시 운용 서버가 `ended_at`을 기록하면 동영상 요청이 자동으로 차단됩니다.

### 도메인 분리 (핸들러 패턴)
`OpsServer`는 연결 수락과 라우팅만 담당하고, 비즈니스 로직은 도메인별 핸들러에 위임합니다. 모든 핸들러는 `BaseHandler`를 상속해 DB·세션·AI풀·설정을 공통으로 주입받습니다. `DictionaryHandler`와 `GameHandler`는 `LearningHandler._request_ai_inference()`를 재사용해 코드 중복을 제거했습니다.

### keypoint 수집 파이프라인
사용자 동의 + 정답 + confidence 임계값을 모두 충족한 경우만 저장합니다. 공수(gongsu) 프레임을 제외하고 `pose(25×2) + left_hand(21×2) + right_hand(21×2) = 134차원` 플랫 배열로 변환해 저장합니다. 누적 데이터가 임계치를 넘으면 재학습이 자동 실행됩니다.

---

## DB 주요 테이블

| 테이블 | 설명 |
|--------|------|
| `users` | 사용자 계정 (우세손, 농인 여부, keypoint 동의 등) |
| `user_session` | 세션 토큰, 시작/종료 시각 (동영상 서버 인증에도 사용) |
| `word_info` | 단어 목록 (1,000개, 난이도·영상경로·word_number 포함) |
| `user_progress` | 사용자별 단어 정확도·시도횟수 누적 |
| `user_history` | 추론 이력 JSON |
| `keypoint_store` | 재학습용 keypoint 데이터 (134차원 플랫 배열) |
| `word_stats` | 단어별 전체 평균 정확도 통계 |
| `game_history` | 게임 이력 (점수, 소요시간, 결과 JSON) |
| `model_versions` | AI 모델 버전 관리 (활성 모델 추적) |

---

## 환경 설정

### 운용 서버 (`config.py`)

| 항목 | 기본값 | 설명 |
|------|--------|------|
| PORT | 9000 | 클라이언트 포트 |
| AI_PORT | 9100 | AI 서버 포트 |
| DB_POOL_MIN/MAX | 5/20 | DB 커넥션 풀 크기 |
| CONFIDENCE_CORRECT | 0.8 | 정답 판정 임계값 |
| CONFIDENCE_CANDIDATE | 0.5 | 재학습 후보 임계값 |
| RETRAIN_HOUR | 3 | 재학습 트리거 시각 |
| RETRAIN_DATA_THRESHOLD | 1000 | 재학습 데이터 임계치 |

### 동영상 서버 (`.env`)

```env
VIDEO_DIR=static/videos
DB_HOST=127.0.0.1
DB_PORT=3306
DB_NAME=ksl_learning
```

---

## 실행 방법

```bash
# 운용 서버
pip install aiomysql
python ops_server_fixed.py

# 동영상 서버
pip install -r requirements.txt
uvicorn main:app --reload --host 0.0.0.0 --port 8000
```
