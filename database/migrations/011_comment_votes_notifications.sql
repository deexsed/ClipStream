-- Голоса за комментарии, in-app уведомления (ответ, подписчик).
-- Идемпотентное применение дублируется в backend/src/main.cpp (CREATE IF NOT EXISTS).

USE clipstream;

CREATE TABLE IF NOT EXISTS comment_votes (
    user_id BIGINT UNSIGNED NOT NULL,
    comment_id BIGINT UNSIGNED NOT NULL,
    vote TINYINT NOT NULL,
    PRIMARY KEY (user_id, comment_id),
    INDEX idx_cv_comment (comment_id),
    CONSTRAINT fk_cv_user FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE,
    CONSTRAINT fk_cv_comment FOREIGN KEY (comment_id) REFERENCES comments(id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS notifications (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id BIGINT UNSIGNED NOT NULL,
    type VARCHAR(32) NOT NULL,
    actor_id BIGINT UNSIGNED NOT NULL,
    payload JSON NULL,
    read_at TIMESTAMP NULL DEFAULT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_notif_user_time (user_id, created_at DESC),
    INDEX idx_notif_user_unread (user_id, read_at),
    CONSTRAINT fk_notif_user FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE,
    CONSTRAINT fk_notif_actor FOREIGN KEY (actor_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB;
