ALTER TABLE users ADD COLUMN account_suspended_until TIMESTAMP NULL DEFAULT NULL;
ALTER TABLE users ADD COLUMN account_ban_reason VARCHAR(512) NULL DEFAULT NULL;
ALTER TABLE users ADD COLUMN account_banned_by BIGINT UNSIGNED NULL DEFAULT NULL;
ALTER TABLE clips ADD COLUMN classification_source ENUM('unknown','current_channel','manual') NOT NULL DEFAULT 'unknown';
UPDATE users SET password_hash='!twitch-only',auth_version=auth_version+1 WHERE twitch_id IS NULL AND password_hash<>'!twitch-only';
ALTER TABLE clips ADD COLUMN moderation_hidden BOOLEAN NOT NULL DEFAULT FALSE;
ALTER TABLE content_reports ADD COLUMN resolution ENUM('dismissed','actioned') NULL;
ALTER TABLE content_reports ADD COLUMN reviewed_by BIGINT UNSIGNED NULL;
ALTER TABLE content_reports ADD COLUMN reviewed_at TIMESTAMP NULL DEFAULT NULL;
ALTER TABLE content_reports ADD COLUMN resolution_reason VARCHAR(512) NOT NULL DEFAULT '';
ALTER TABLE comments ADD INDEX idx_comments_roots (clip_id,parent_id,created_at,id);
CREATE TABLE IF NOT EXISTS admin_bootstrap (
 id TINYINT UNSIGNED PRIMARY KEY,
 eligible BOOLEAN NOT NULL,
 twitch_id VARCHAR(64) NULL,
 completed_by BIGINT UNSIGNED NULL,
 completed_at TIMESTAMP NULL DEFAULT NULL,
 CONSTRAINT chk_bootstrap_singleton CHECK(id=1)
) ENGINE=InnoDB;
INSERT IGNORE INTO admin_bootstrap(id,eligible) SELECT 1,COUNT(*)=0 FROM users;
CREATE TABLE IF NOT EXISTS ip_bans (
 id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
 ip VARCHAR(45) NOT NULL UNIQUE,
 reason VARCHAR(512) NOT NULL,
 actor_id BIGINT UNSIGNED NOT NULL,
 expires_at TIMESTAMP NULL DEFAULT NULL,
 created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS moderation_audit (
 id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
 actor_id BIGINT UNSIGNED NOT NULL,
 target_type VARCHAR(16) NOT NULL,
 target_id VARCHAR(64) NOT NULL,
 action VARCHAR(32) NOT NULL,
 reason VARCHAR(512) NOT NULL DEFAULT '',
 details JSON NULL,
 created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
 INDEX idx_moderation_time(created_at,id)
) ENGINE=InnoDB;
