ALTER TABLE clip_sync_games ADD COLUMN fresh_empty_streak INT UNSIGNED NOT NULL DEFAULT 0;
ALTER TABLE clip_sync_games ADD COLUMN fresh_next_poll_at BIGINT NOT NULL DEFAULT 0;
ALTER TABLE clip_sync_games ADD COLUMN fresh_feedback_end BIGINT NOT NULL DEFAULT 0;
ALTER TABLE clip_sync_games ADD COLUMN fresh_productive_end BIGINT NOT NULL DEFAULT 0;
ALTER TABLE clip_sync_games ADD INDEX idx_csg_poll(fresh_next_poll_at,scheduled_at,game_id);
