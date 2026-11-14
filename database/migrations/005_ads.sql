-- Campaign counters count served reservations and acknowledged client events separately.
CREATE TABLE IF NOT EXISTS ad_campaigns (
 id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
 name VARCHAR(160) NOT NULL,
 type VARCHAR(16) NOT NULL,
 title VARCHAR(240) NOT NULL,
 body TEXT NOT NULL,
 advertiser VARCHAR(240) NOT NULL,
 disclosure_token VARCHAR(240) NOT NULL DEFAULT '',
 image_url VARCHAR(2048) NOT NULL DEFAULT '',
 video_url VARCHAR(2048) NOT NULL DEFAULT '',
 click_url VARCHAR(2048) NOT NULL,
 enabled BOOLEAN NOT NULL DEFAULT FALSE,
 priority INT UNSIGNED NOT NULL DEFAULT 0,
 starts_at DATETIME NULL,
 ends_at DATETIME NULL,
 daily_cap INT UNSIGNED NOT NULL DEFAULT 3,
 cooldown_seconds INT UNSIGNED NOT NULL DEFAULT 300,
 skip_after_seconds INT UNSIGNED NOT NULL DEFAULT 0,
 served BIGINT UNSIGNED NOT NULL DEFAULT 0,
 impressions BIGINT UNSIGNED NOT NULL DEFAULT 0,
 clicks BIGINT UNSIGNED NOT NULL DEFAULT 0,
 deleted_at DATETIME NULL,
 created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
 updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
 INDEX idx_ads_active (type, enabled, deleted_at, priority)
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS ad_daily_caps (
 campaign_id BIGINT UNSIGNED NOT NULL,
 audience_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 day DATE NOT NULL,
 served INT UNSIGNED NOT NULL DEFAULT 0,
 last_served_at DATETIME NOT NULL,
 PRIMARY KEY (campaign_id, audience_hash, day),
 INDEX idx_ads_cap_retention (day),
 FOREIGN KEY (campaign_id) REFERENCES ad_campaigns(id)
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS ad_deliveries (
 token_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL PRIMARY KEY,
 campaign_id BIGINT UNSIGNED NOT NULL,
 creative JSON NOT NULL,
 expires_at DATETIME NOT NULL,
 impression_at DATETIME NULL,
 click_at DATETIME NULL,
 created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
 INDEX idx_ads_delivery_retention (expires_at),
 FOREIGN KEY (campaign_id) REFERENCES ad_campaigns(id)
) ENGINE=InnoDB;
