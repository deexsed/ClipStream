-- Согласие с документами при входе через Twitch
ALTER TABLE users ADD COLUMN legal_consent_at VARCHAR(64) NULL DEFAULT NULL;
ALTER TABLE users ADD COLUMN legal_consent_version VARCHAR(32) NULL DEFAULT NULL;
