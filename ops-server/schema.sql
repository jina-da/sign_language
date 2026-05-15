-- SignLearn MariaDB 스키마
-- 9개 테이블: users / user_progress / user_history / keypoint_store
--             user_session / word_info / word_stats / model_versions / game_history

CREATE DATABASE IF NOT EXISTS signlearn_db
    CHARACTER SET utf8mb4
    COLLATE utf8mb4_unicode_ci;

USE signlearn_db;

-- ──────────────────────────────────────────
-- 1. 사용자
-- ──────────────────────────────────────────
CREATE TABLE IF NOT EXISTS users (
    id                INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    username          VARCHAR(50)  NOT NULL UNIQUE,
    password_hash     VARCHAR(128) NOT NULL,          -- SHA-256 or bcrypt
    is_deaf           TINYINT(1)   NOT NULL DEFAULT 0,
    is_dominant_left  TINYINT(1)   NOT NULL DEFAULT 0,
    keypoint_consent  TINYINT(1)   NOT NULL DEFAULT 0,
    is_active         TINYINT(1)   NOT NULL DEFAULT 1,
    daily_goal        TINYINT UNSIGNED NOT NULL DEFAULT 10,  -- 5~30
    created_at        DATETIME     NOT NULL DEFAULT NOW(),
    updated_at        DATETIME     NOT NULL DEFAULT NOW() ON UPDATE NOW(),
    INDEX idx_username (username)
) ENGINE=InnoDB;

-- ──────────────────────────────────────────
-- 2. 단어별 학습 진도
-- ──────────────────────────────────────────
CREATE TABLE IF NOT EXISTS user_progress (
    id             BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id        INT UNSIGNED    NOT NULL,
    word_id        INT UNSIGNED    NOT NULL,
    accuracy       FLOAT           NOT NULL DEFAULT 0.0,  -- 0.0 ~ 1.0 이동평균
    attempts       INT UNSIGNED    NOT NULL DEFAULT 0,
    last_practiced DATETIME,
    UNIQUE KEY uq_user_word (user_id, word_id),
    INDEX idx_user_id (user_id),
    INDEX idx_review   (user_id, accuracy, attempts)   -- 복습 쿼리 최적화
) ENGINE=InnoDB;

-- ──────────────────────────────────────────
-- 3. 학습 이력 (JSON 컬럼 방식 - 행 수 최소화)
-- ──────────────────────────────────────────
CREATE TABLE IF NOT EXISTS user_history (
    id           BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id      INT UNSIGNED  NOT NULL,
    session_id   VARCHAR(36)   NOT NULL,
    history_json JSON          NOT NULL,  -- {word_id, result, accuracy, timestamp}
    created_at   DATETIME      NOT NULL DEFAULT NOW(),
    INDEX idx_user_session (user_id, session_id),
    INDEX idx_created      (created_at)
) ENGINE=InnoDB;

-- ──────────────────────────────────────────
-- 4. Keypoint 저장소 (재학습 데이터)
-- ──────────────────────────────────────────
CREATE TABLE IF NOT EXISTS keypoint_store (
    id             BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id        INT UNSIGNED  NOT NULL,
    word_id        INT UNSIGNED  NOT NULL,
    keypoint_data  JSON          NOT NULL,  -- list[list[float]]
    confidence     FLOAT         NOT NULL,
    is_trained     TINYINT(1)    NOT NULL DEFAULT 0,
    created_at     DATETIME      NOT NULL DEFAULT NOW(),
    INDEX idx_is_trained  (is_trained, created_at),  -- 재학습 쿼리
    INDEX idx_user_word   (user_id, word_id)
) ENGINE=InnoDB;

-- ──────────────────────────────────────────
-- 5. 세션
-- ──────────────────────────────────────────
CREATE TABLE IF NOT EXISTS user_session (
    session_id    VARCHAR(36)   PRIMARY KEY,
    user_id       INT UNSIGNED  NOT NULL,
    started_at    DATETIME      NOT NULL DEFAULT NOW(),
    last_active   DATETIME      NOT NULL DEFAULT NOW(),
    ended_at      DATETIME,
    word_count    INT UNSIGNED  NOT NULL DEFAULT 0,
    duration_sec  INT UNSIGNED  NOT NULL DEFAULT 0,
    INDEX idx_user_id (user_id)
) ENGINE=InnoDB;

-- ──────────────────────────────────────────
-- 6. 단어 정보
-- ──────────────────────────────────────────
CREATE TABLE IF NOT EXISTS word_info (
    id         INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    word       VARCHAR(200)  NOT NULL,
    meaning    TEXT,
    difficulty TINYINT UNSIGNED NOT NULL DEFAULT 1,
    video_path VARCHAR(300),
    INDEX idx_word       (word),
    INDEX idx_difficulty (difficulty)
) ENGINE=InnoDB;

-- ──────────────────────────────────────────
-- 7. 단어별 전체 통계
-- ──────────────────────────────────────────
CREATE TABLE IF NOT EXISTS word_stats (
    word_id        INT UNSIGNED PRIMARY KEY,
    avg_accuracy   FLOAT        NOT NULL DEFAULT 0.0,
    total_attempts BIGINT UNSIGNED NOT NULL DEFAULT 0,
    updated_at     DATETIME     NOT NULL DEFAULT NOW() ON UPDATE NOW()
) ENGINE=InnoDB;

-- ──────────────────────────────────────────
-- 8. 모델 버전 관리
-- ──────────────────────────────────────────
CREATE TABLE IF NOT EXISTS model_versions (
    id          INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    version     VARCHAR(20)  NOT NULL,     -- v1.0, v1.1 ...
    accuracy    FLOAT        NOT NULL,
    file_path   VARCHAR(255) NOT NULL,
    is_active   TINYINT(1)   NOT NULL DEFAULT 0,
    data_count  INT UNSIGNED NOT NULL DEFAULT 0,
    trained_at  DATETIME     NOT NULL DEFAULT NOW(),
    INDEX idx_is_active (is_active)
) ENGINE=InnoDB;

-- ──────────────────────────────────────────
-- 9. 게임 이력
-- ──────────────────────────────────────────
CREATE TABLE IF NOT EXISTS game_history (
    id               BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id          INT UNSIGNED  NOT NULL,
    mode             VARCHAR(20)   NOT NULL,  -- time_attack | survival
    score            INT           NOT NULL DEFAULT 0,
    duration_sec     INT UNSIGNED  NOT NULL DEFAULT 0,
    word_results_json JSON,
    played_at        DATETIME      NOT NULL DEFAULT NOW(),
    INDEX idx_user_played (user_id, played_at)
) ENGINE=InnoDB;
