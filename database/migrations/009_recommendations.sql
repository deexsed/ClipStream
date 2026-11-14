-- New recommendation measurements start here. Old heartbeat/impression data is not backfilled.
CREATE TABLE IF NOT EXISTS recommendation_activity (
 user_id BIGINT UNSIGNED NOT NULL,
 clip_id BIGINT UNSIGNED NOT NULL,
 first_opened_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
 last_opened_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
 PRIMARY KEY(user_id,clip_id),
 INDEX idx_ra_clip(clip_id,last_opened_at,user_id),
 INDEX idx_ra_recent(last_opened_at,user_id),
 FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE,
 FOREIGN KEY(clip_id) REFERENCES clips(id) ON DELETE CASCADE
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS recommendation_deliveries (
 delivery_id CHAR(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL PRIMARY KEY,
 user_id BIGINT UNSIGNED NULL,
 clip_id BIGINT UNSIGNED NOT NULL,
 surface VARCHAR(16) NOT NULL,
 source VARCHAR(24) NOT NULL,
 position INT UNSIGNED NOT NULL,
 algorithm_version INT UNSIGNED NOT NULL,
 served_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
 impressed_at TIMESTAMP NULL DEFAULT NULL,
 INDEX idx_rd_clip(clip_id,served_at),
 INDEX idx_rd_expiry(served_at),
 FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE,
 FOREIGN KEY(clip_id) REFERENCES clips(id) ON DELETE CASCADE
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS clip_twitch_snapshots (
 clip_id BIGINT UNSIGNED NOT NULL,
 observed_at TIMESTAMP NOT NULL,
 view_count BIGINT UNSIGNED NOT NULL,
 PRIMARY KEY(clip_id,observed_at),
 INDEX idx_cts_expiry(observed_at),
 FOREIGN KEY(clip_id) REFERENCES clips(id) ON DELETE CASCADE
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS recommendation_clip_features (
 clip_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
 likes BIGINT UNSIGNED NOT NULL DEFAULT 0,
 dislikes BIGINT UNSIGNED NOT NULL DEFAULT 0,
 unique_opens BIGINT UNSIGNED NOT NULL DEFAULT 0,
 recent_opens BIGINT UNSIGNED NOT NULL DEFAULT 0,
 commenters BIGINT UNSIGNED NOT NULL DEFAULT 0,
 dismissals BIGINT UNSIGNED NOT NULL DEFAULT 0,
 impressions BIGINT UNSIGNED NOT NULL DEFAULT 0,
 twitch_velocity DOUBLE NOT NULL DEFAULT 0,
 audience_score DOUBLE NOT NULL DEFAULT 0,
 computed_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
 INDEX idx_rcf_audience(audience_score DESC,clip_id),
 FOREIGN KEY(clip_id) REFERENCES clips(id) ON DELETE CASCADE
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS recommendation_predictions (
 user_id BIGINT UNSIGNED NOT NULL,
 clip_id BIGINT UNSIGNED NOT NULL,
 score DOUBLE NOT NULL,
 support INT UNSIGNED NOT NULL,
 computed_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
 PRIMARY KEY(user_id,clip_id),
 INDEX idx_rp_score(user_id,score DESC),
 FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE,
 FOREIGN KEY(clip_id) REFERENCES clips(id) ON DELETE CASCADE
) ENGINE=InnoDB;
ALTER TABLE likes ADD INDEX idx_likes_clip(clip_id,is_like,user_id);
ALTER TABLE likes ADD INDEX idx_likes_recent(created_at,user_id);
ALTER TABLE comments ADD INDEX idx_comments_user(user_id,created_at,clip_id);
ALTER TABLE user_feed_blocks ADD INDEX idx_ufb_target(target_key,user_id);
ALTER TABLE clips ADD INDEX idx_clips_channel_date(broadcaster_id,twitch_created_ts,id);
ALTER TABLE recommendation_deliveries ADD INDEX idx_rd_frequency(user_id,clip_id,surface,impressed_at);
ALTER TABLE clips ADD COLUMN discovery_bucket TINYINT UNSIGNED NOT NULL DEFAULT 0;
UPDATE clips SET discovery_bucket=MOD(CRC32(twitch_clip_id),32);
ALTER TABLE clips ADD INDEX idx_clips_discovery(discovery_bucket,twitch_created_ts,id);
