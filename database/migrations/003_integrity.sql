ALTER TABLE users ADD COLUMN auth_version BIGINT UNSIGNED NOT NULL DEFAULT 0;
ALTER TABLE users ADD COLUMN oauth_validated_at TIMESTAMP NULL DEFAULT NULL;
ALTER TABLE clips ADD COLUMN broadcaster_login VARCHAR(128) NOT NULL DEFAULT '';
UPDATE users SET twitch_id=NULL WHERE twitch_id='';
UPDATE users SET password_hash='!oauth-only',auth_version=auth_version+1 WHERE twitch_id IS NOT NULL AND password_hash<>'!oauth-only';
CREATE TABLE IF NOT EXISTS analytics_clip_sessions (
 user_id BIGINT UNSIGNED NOT NULL,
 session_id VARCHAR(64) NOT NULL,
 clip_id BIGINT UNSIGNED NOT NULL,
 watched_sec INT UNSIGNED NOT NULL DEFAULT 0,
 finalized BOOLEAN NOT NULL DEFAULT FALSE,
 updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
 PRIMARY KEY (user_id,session_id,clip_id),
 FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE,
 FOREIGN KEY (clip_id) REFERENCES clips(id) ON DELETE CASCADE
) ENGINE=InnoDB;
