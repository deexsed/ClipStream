-- Журнал compliance (DMCA, удаления, недоступность)
CREATE TABLE IF NOT EXISTS compliance_log (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    action VARCHAR(64) NOT NULL,
    clip_id BIGINT UNSIGNED NULL,
    actor_user_id BIGINT UNSIGNED NULL,
    details_json JSON NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_cl_clip (clip_id),
    INDEX idx_cl_time (created_at DESC)
) ENGINE=InnoDB;
