-- История бейджей (репутация / аудит)
CREATE TABLE IF NOT EXISTS user_badge_history (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id BIGINT UNSIGNED NOT NULL,
    badge VARCHAR(32) NOT NULL,
    event_type ENUM('granted','revoked') NOT NULL DEFAULT 'granted',
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_ubh_user (user_id, created_at DESC),
    CONSTRAINT fk_ubh_user FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB;
