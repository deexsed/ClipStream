CREATE TABLE IF NOT EXISTS recommendation_dirty_clips (
 clip_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
 generation BIGINT UNSIGNED NOT NULL DEFAULT 1,
 queued_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
 INDEX idx_rdc_queue(queued_at,clip_id),
 FOREIGN KEY(clip_id) REFERENCES clips(id) ON DELETE CASCADE
) ENGINE=InnoDB;
ALTER TABLE likes ADD INDEX idx_likes_user_recent(user_id,created_at,clip_id);
ALTER TABLE clips ADD INDEX idx_clips_game_date(game_id,twitch_created_ts,id);
CREATE TABLE IF NOT EXISTS recommendation_twitch_follows (
 user_id BIGINT UNSIGNED NOT NULL,
 broadcaster_id VARCHAR(64) NOT NULL,
 refreshed_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
 PRIMARY KEY(user_id,broadcaster_id),
 FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS recommendation_active_users (
 user_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
 touched_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
 FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB;
INSERT IGNORE INTO recommendation_active_users(user_id) SELECT DISTINCT user_id FROM likes;
ALTER TABLE recommendation_activity ADD INDEX idx_ra_user_recent(user_id,last_opened_at,clip_id);
