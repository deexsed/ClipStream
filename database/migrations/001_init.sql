CREATE DATABASE IF NOT EXISTS clipstream
    CHARACTER SET utf8mb4
    COLLATE utf8mb4_unicode_ci;

USE clipstream;

-- ============================================================
-- Пользователи
-- ============================================================
CREATE TABLE users (
    id            BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    username      VARCHAR(64)  NOT NULL UNIQUE,
    email         VARCHAR(255) NOT NULL UNIQUE,
    password_hash VARCHAR(255) NOT NULL,
    avatar_url    VARCHAR(512) DEFAULT NULL,
    twitch_id     VARCHAR(64)  DEFAULT NULL UNIQUE,
    twitch_access_token TEXT DEFAULT NULL,
    twitch_refresh_token TEXT DEFAULT NULL,
    twitch_token_expires_at VARCHAR(64) DEFAULT NULL,
    role          ENUM('user', 'moderator', 'admin') NOT NULL DEFAULT 'user',
    reputation    INT          NOT NULL DEFAULT 0,
    badge         ENUM('none', 'rising_star', 'pro', 'legend') NOT NULL DEFAULT 'none',
    created_at    TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at    TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,

    INDEX idx_users_twitch (twitch_id),
    INDEX idx_users_reputation (reputation DESC)
) ENGINE=InnoDB;

-- ============================================================
-- Игры
-- ============================================================
CREATE TABLE games (
    id         BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    twitch_id  VARCHAR(64) NOT NULL UNIQUE,
    name       VARCHAR(255) NOT NULL,
    box_art    VARCHAR(512) DEFAULT NULL,
    created_at TIMESTAMP   NOT NULL DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB;

-- ============================================================
-- Клипы
-- ============================================================
CREATE TABLE clips (
    id              BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    twitch_clip_id  VARCHAR(128) NOT NULL UNIQUE,
    title           VARCHAR(512) NOT NULL,
    title_fingerprint VARCHAR(16) NOT NULL DEFAULT '',
    thumbnail_url   VARCHAR(512) NOT NULL,
    embed_url       VARCHAR(512) NOT NULL,
    duration        FLOAT        NOT NULL DEFAULT 0,
    view_count      INT UNSIGNED NOT NULL DEFAULT 0,
    like_count      INT          NOT NULL DEFAULT 0,
    dislike_count   INT          NOT NULL DEFAULT 0,
    broadcaster_name VARCHAR(255) NOT NULL,
    game_id         BIGINT UNSIGNED DEFAULT NULL,
    author_id       BIGINT UNSIGNED DEFAULT NULL,
    created_at      TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,
    fetched_at      TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,

    INDEX idx_clips_title_fp  (title_fingerprint),
    INDEX idx_clips_game      (game_id),
    INDEX idx_clips_author    (author_id),
    INDEX idx_clips_views     (view_count DESC),
    INDEX idx_clips_likes     (like_count DESC),
    INDEX idx_clips_created   (created_at DESC),
    FULLTEXT INDEX idx_clips_search (title, broadcaster_name),

    CONSTRAINT fk_clips_game   FOREIGN KEY (game_id)   REFERENCES games(id) ON DELETE SET NULL,
    CONSTRAINT fk_clips_author FOREIGN KEY (author_id) REFERENCES users(id) ON DELETE SET NULL
) ENGINE=InnoDB;

-- ============================================================
-- Теги
-- ============================================================
CREATE TABLE tags (
    id   BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(64) NOT NULL UNIQUE
) ENGINE=InnoDB;

CREATE TABLE clip_tags (
    clip_id BIGINT UNSIGNED NOT NULL,
    tag_id  BIGINT UNSIGNED NOT NULL,
    PRIMARY KEY (clip_id, tag_id),
    CONSTRAINT fk_ct_clip FOREIGN KEY (clip_id) REFERENCES clips(id) ON DELETE CASCADE,
    CONSTRAINT fk_ct_tag  FOREIGN KEY (tag_id)  REFERENCES tags(id)  ON DELETE CASCADE
) ENGINE=InnoDB;

-- ============================================================
-- Лайки / дизлайки
-- ============================================================
CREATE TABLE likes (
    user_id  BIGINT UNSIGNED NOT NULL,
    clip_id  BIGINT UNSIGNED NOT NULL,
    is_like  BOOLEAN NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (user_id, clip_id),
    CONSTRAINT fk_likes_user FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE,
    CONSTRAINT fk_likes_clip FOREIGN KEY (clip_id) REFERENCES clips(id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- ============================================================
-- Комментарии (древовидные)
-- ============================================================
CREATE TABLE comments (
    id         BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    clip_id    BIGINT UNSIGNED NOT NULL,
    user_id    BIGINT UNSIGNED NOT NULL,
    parent_id  BIGINT UNSIGNED DEFAULT NULL,
    body       TEXT NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,

    INDEX idx_comments_clip   (clip_id, created_at),
    INDEX idx_comments_parent (parent_id),

    CONSTRAINT fk_comments_clip   FOREIGN KEY (clip_id)   REFERENCES clips(id)    ON DELETE CASCADE,
    CONSTRAINT fk_comments_user   FOREIGN KEY (user_id)   REFERENCES users(id)    ON DELETE CASCADE,
    CONSTRAINT fk_comments_parent FOREIGN KEY (parent_id) REFERENCES comments(id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- ============================================================
-- Подписки
-- ============================================================
CREATE TABLE subscriptions (
    follower_id  BIGINT UNSIGNED NOT NULL,
    followed_id  BIGINT UNSIGNED NOT NULL,
    created_at   TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (follower_id, followed_id),
    CONSTRAINT fk_sub_follower FOREIGN KEY (follower_id) REFERENCES users(id) ON DELETE CASCADE,
    CONSTRAINT fk_sub_followed FOREIGN KEY (followed_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- ============================================================
-- История просмотров
-- ============================================================
CREATE TABLE view_history (
    id        BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id   BIGINT UNSIGNED NOT NULL,
    clip_id   BIGINT UNSIGNED NOT NULL,
    viewed_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,

    INDEX idx_vh_user (user_id, viewed_at DESC),
    CONSTRAINT fk_vh_user FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE,
    CONSTRAINT fk_vh_clip FOREIGN KEY (clip_id) REFERENCES clips(id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- ============================================================
-- Кэш рассчитанных рекомендаций стримеров
-- ============================================================
CREATE TABLE user_recommendations (
    user_id BIGINT UNSIGNED PRIMARY KEY,
    recommended_broadcasters_json JSON DEFAULT NULL,
    calculated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT fk_user_reco_user FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- ============================================================
-- Сигналы просмотра и негативная персонализация (лента)
-- ============================================================
CREATE TABLE user_clip_engagement (
    user_id BIGINT UNSIGNED NOT NULL,
    clip_id BIGINT UNSIGNED NOT NULL,
    watch_time_sec INT UNSIGNED NOT NULL DEFAULT 0,
    max_position_sec INT UNSIGNED NOT NULL DEFAULT 0,
    completion_ratio FLOAT NOT NULL DEFAULT 0,
    early_skip TINYINT(1) NOT NULL DEFAULT 0,
    replay_count INT UNSIGNED NOT NULL DEFAULT 0,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (user_id, clip_id),
    CONSTRAINT fk_uce_user FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE,
    CONSTRAINT fk_uce_clip FOREIGN KEY (clip_id) REFERENCES clips(id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE user_feed_blocks (
    user_id BIGINT UNSIGNED NOT NULL,
    target_key VARCHAR(160) NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (user_id, target_key),
    CONSTRAINT fk_ufb_user FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- ============================================================
-- Витрина признаков (feature store) для ранжирования и офлайн-аналитики
-- ============================================================
CREATE TABLE feature_store_clip (
    clip_id BIGINT UNSIGNED PRIMARY KEY,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    age_hours FLOAT NOT NULL DEFAULT 0,
    velocity_score FLOAT NOT NULL DEFAULT 0,
    er_like FLOAT NOT NULL DEFAULT 0,
    freshness_decay FLOAT NOT NULL DEFAULT 0,
    CONSTRAINT fk_fsc_clip FOREIGN KEY (clip_id) REFERENCES clips(id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE feature_store_user (
    user_id BIGINT UNSIGNED PRIMARY KEY,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    top_games_json JSON NULL,
    affinity_entropy FLOAT NULL,
    watch_sec_7d INT UNSIGNED NOT NULL DEFAULT 0,
    clips_viewed_7d INT UNSIGNED NOT NULL DEFAULT 0,
    CONSTRAINT fk_fsu_user FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- ============================================================
-- Продуктовая аналитика (сессии, события, DAU-подобные срезы)
-- ============================================================
CREATE TABLE analytics_sessions (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id BIGINT UNSIGNED NOT NULL,
    session_id VARCHAR(64) NOT NULL,
    started_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    last_seen_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    total_watch_sec INT UNSIGNED NOT NULL DEFAULT 0,
    clips_viewed INT UNSIGNED NOT NULL DEFAULT 0,
    UNIQUE KEY uq_analytics_session (user_id, session_id),
    INDEX idx_analytics_sess_user (user_id, last_seen_at),
    CONSTRAINT fk_as_user FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE analytics_feed_events (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id BIGINT UNSIGNED NOT NULL,
    session_id VARCHAR(64) DEFAULT NULL,
    event_type ENUM('watch_heartbeat','watch_session_end','home_impression') NOT NULL,
    block_type VARCHAR(32) NOT NULL DEFAULT '',
    clip_id BIGINT UNSIGNED DEFAULT NULL,
    value DOUBLE NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_afe_user_time (user_id, created_at),
    INDEX idx_afe_event (event_type, created_at),
    CONSTRAINT fk_afe_user FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE analytics_user_daily (
    user_id BIGINT UNSIGNED NOT NULL,
    activity_date DATE NOT NULL,
    watch_sec INT UNSIGNED NOT NULL DEFAULT 0,
    clips_viewed INT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (user_id, activity_date),
    CONSTRAINT fk_aud_user FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- ============================================================
-- Настройки приложения (runtime config)
-- ============================================================
CREATE TABLE app_settings (
    setting_key VARCHAR(128) PRIMARY KEY,
    setting_value TEXT NOT NULL,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB;

-- ============================================================
-- Рекламные баннеры
-- ============================================================
CREATE TABLE ads (
    id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    title       VARCHAR(255) NOT NULL,
    image_url   VARCHAR(512) DEFAULT NULL,
    video_url   VARCHAR(512) DEFAULT NULL,
    target_url  VARCHAR(512) NOT NULL,
    position    INT UNSIGNED NOT NULL DEFAULT 5,
    is_active   BOOLEAN NOT NULL DEFAULT TRUE,
    starts_at   TIMESTAMP NULL,
    ends_at     TIMESTAMP NULL,
    created_at  TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB;
