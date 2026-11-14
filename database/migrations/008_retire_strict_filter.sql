-- Retained legacy column is inert. Clear old choices and prevent old defaults
-- from re-enabling the retired setting on newly inserted settings rows.
ALTER TABLE user_settings ALTER COLUMN hide_mature SET DEFAULT 0;
UPDATE user_settings SET hide_mature=0;
