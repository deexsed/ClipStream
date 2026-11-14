-- A permanent restriction must not expire at the legacy TIMESTAMP sentinel.
ALTER TABLE users ADD COLUMN chat_banned_permanently BOOLEAN NOT NULL DEFAULT FALSE;
UPDATE users SET chat_banned_permanently=TRUE,chat_banned_until=NULL WHERE chat_banned_until='2038-01-01 00:00:00';
