-- Друзья (заявки + двусторонние связи), стена профиля с упоминаниями, зафиксированные достижения

CREATE TABLE IF NOT EXISTS friendships (
  user_id BIGINT UNSIGNED NOT NULL,
  friend_id BIGINT UNSIGNED NOT NULL,
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (user_id, friend_id),
  INDEX idx_friends_friend (friend_id),
  CONSTRAINT fk_friends_user FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE,
  CONSTRAINT fk_friends_friend FOREIGN KEY (friend_id) REFERENCES users(id) ON DELETE CASCADE,
  CONSTRAINT chk_friends_ne CHECK (user_id <> friend_id)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS friend_requests (
  requester_id BIGINT UNSIGNED NOT NULL,
  addressee_id BIGINT UNSIGNED NOT NULL,
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (requester_id, addressee_id),
  INDEX idx_fr_addressee (addressee_id),
  CONSTRAINT fk_fr_requester FOREIGN KEY (requester_id) REFERENCES users(id) ON DELETE CASCADE,
  CONSTRAINT fk_fr_addressee FOREIGN KEY (addressee_id) REFERENCES users(id) ON DELETE CASCADE,
  CONSTRAINT chk_fr_ne CHECK (requester_id <> addressee_id)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS user_achievements (
  user_id BIGINT UNSIGNED NOT NULL,
  achievement_key VARCHAR(64) NOT NULL,
  earned_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (user_id, achievement_key),
  INDEX idx_ua_earned (user_id, earned_at DESC),
  CONSTRAINT fk_ua_user FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS profile_wall_posts (
  id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  profile_owner_id BIGINT UNSIGNED NOT NULL,
  author_id BIGINT UNSIGNED NOT NULL,
  body TEXT NOT NULL,
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  INDEX idx_wall_owner_time (profile_owner_id, created_at DESC),
  CONSTRAINT fk_wall_owner FOREIGN KEY (profile_owner_id) REFERENCES users(id) ON DELETE CASCADE,
  CONSTRAINT fk_wall_author FOREIGN KEY (author_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS profile_wall_mentions (
  id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  post_id BIGINT UNSIGNED NOT NULL,
  mention_kind ENUM('user','clip') NOT NULL,
  mentioned_user_id BIGINT UNSIGNED NULL,
  mentioned_clip_id BIGINT UNSIGNED NULL,
  INDEX idx_wm_post (post_id),
  CONSTRAINT fk_wm_post FOREIGN KEY (post_id) REFERENCES profile_wall_posts(id) ON DELETE CASCADE,
  CONSTRAINT fk_wm_user FOREIGN KEY (mentioned_user_id) REFERENCES users(id) ON DELETE CASCADE,
  CONSTRAINT fk_wm_clip FOREIGN KEY (mentioned_clip_id) REFERENCES clips(id) ON DELETE CASCADE
) ENGINE=InnoDB;
