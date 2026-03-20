-- Жалобы на контент
CREATE TABLE IF NOT EXISTS content_reports (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    reporter_id BIGINT UNSIGNED NOT NULL,
    target_type ENUM('clip','comment','user') NOT NULL,
    target_id BIGINT UNSIGNED NOT NULL,
    reason VARCHAR(512) NOT NULL DEFAULT '',
    status ENUM('open','reviewed','dismissed') NOT NULL DEFAULT 'open',
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_cr_open (status, created_at DESC),
    CONSTRAINT fk_cr_reporter FOREIGN KEY (reporter_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- Настройки ленты пользователя
CREATE TABLE IF NOT EXISTS user_settings (
    user_id BIGINT UNSIGNED PRIMARY KEY,
    hide_mature TINYINT(1) NOT NULL DEFAULT 1,
    mute_keywords JSON NULL,
    CONSTRAINT fk_us_user FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB;
