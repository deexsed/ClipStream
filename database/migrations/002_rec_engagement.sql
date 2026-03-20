-- Рекомендации: engagement, блокировки, title_fingerprint
-- Идемпотентное применение см. backend/src/main.cpp (column_exists / CREATE IF NOT EXISTS)

USE clipstream;

ALTER TABLE clips ADD COLUMN title_fingerprint VARCHAR(16) NOT NULL DEFAULT '' AFTER title;
CREATE INDEX idx_clips_title_fp ON clips (title_fingerprint);

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
