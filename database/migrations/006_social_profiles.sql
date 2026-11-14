CREATE TABLE IF NOT EXISTS profile_entities (
    profile_key VARCHAR(96) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
    twitch_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NULL UNIQUE,
    local_user_id BIGINT UNSIGNED NULL UNIQUE,
    last_login VARCHAR(64) NOT NULL DEFAULT '',
    display_name VARCHAR(128) NOT NULL DEFAULT '',
    avatar_url VARCHAR(512) NOT NULL DEFAULT '',
    refreshed_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT fk_profile_entity_user FOREIGN KEY (local_user_id) REFERENCES users(id) ON DELETE SET NULL
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS profile_follows (
    follower_user_id BIGINT UNSIGNED NOT NULL,
    profile_key VARCHAR(96) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY(follower_user_id,profile_key),
    INDEX idx_profile_followers(profile_key,created_at,follower_user_id),
    CONSTRAINT fk_pf_user FOREIGN KEY(follower_user_id) REFERENCES users(id) ON DELETE CASCADE,
    CONSTRAINT fk_pf_entity FOREIGN KEY(profile_key) REFERENCES profile_entities(profile_key) ON DELETE CASCADE
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS user_profile_settings (
    user_id BIGINT UNSIGNED PRIMARY KEY,
    display_name VARCHAR(128) NOT NULL DEFAULT '',
    bio VARCHAR(2000) NOT NULL DEFAULT '',
    website VARCHAR(512) NOT NULL DEFAULT '',
    profile_visibility ENUM('public','friends','private') NOT NULL DEFAULT 'public',
    followers_visibility ENUM('public','friends','private') NOT NULL DEFAULT 'public',
    following_visibility ENUM('public','friends','private') NOT NULL DEFAULT 'private',
    wall_policy ENUM('everyone','friends','none') NOT NULL DEFAULT 'friends',
    CONSTRAINT fk_ups_user FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB;
INSERT IGNORE INTO profile_entities(profile_key,twitch_id,local_user_id,last_login,display_name,avatar_url)
SELECT IF(twitch_id IS NOT NULL AND twitch_id<>'',CONCAT('t-',twitch_id),CONCAT('u-',id)),
       NULLIF(twitch_id,''),id,IF(twitch_id IS NOT NULL AND twitch_id<>'',username,''),username,COALESCE(avatar_url,'') FROM users;
-- Preserve existing one-way site subscriptions, independently of friendships and Twitch follows.
INSERT IGNORE INTO profile_follows(follower_user_id,profile_key,created_at)
SELECT s.follower_id,p.profile_key,s.created_at FROM subscriptions s JOIN profile_entities p ON p.local_user_id=s.followed_id WHERE s.follower_id<>s.followed_id;

ALTER TABLE notifications ADD COLUMN wall_post_id BIGINT UNSIGNED GENERATED ALWAYS AS (CASE WHEN type='wall_mention' THEN CAST(JSON_UNQUOTE(JSON_EXTRACT(payload,'$.post_id')) AS UNSIGNED) ELSE NULL END) VIRTUAL;
ALTER TABLE notifications ADD INDEX idx_notifications_wall_post(wall_post_id);
