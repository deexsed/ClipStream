-- Витрина признаков + аналитика (дубликат для ручного применения; bootstrap в main.cpp)

USE clipstream;

CREATE TABLE IF NOT EXISTS feature_store_clip (
    clip_id BIGINT UNSIGNED PRIMARY KEY,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    age_hours FLOAT NOT NULL DEFAULT 0,
    velocity_score FLOAT NOT NULL DEFAULT 0,
    er_like FLOAT NOT NULL DEFAULT 0,
    freshness_decay FLOAT NOT NULL DEFAULT 0,
    CONSTRAINT fk_fsc_clip FOREIGN KEY (clip_id) REFERENCES clips(id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS feature_store_user (
    user_id BIGINT UNSIGNED PRIMARY KEY,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    top_games_json JSON NULL,
    affinity_entropy FLOAT NULL,
    watch_sec_7d INT UNSIGNED NOT NULL DEFAULT 0,
    clips_viewed_7d INT UNSIGNED NOT NULL DEFAULT 0,
    CONSTRAINT fk_fsu_user FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS analytics_sessions (
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

CREATE TABLE IF NOT EXISTS analytics_feed_events (
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

CREATE TABLE IF NOT EXISTS analytics_user_daily (
    user_id BIGINT UNSIGNED NOT NULL,
    activity_date DATE NOT NULL,
    watch_sec INT UNSIGNED NOT NULL DEFAULT 0,
    clips_viewed INT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (user_id, activity_date),
    CONSTRAINT fk_aud_user FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB;
