-- Индексируемая дата клипа + индекс просмотров (user_id, clip_id) для быстрой ленты.
-- На уже заполненной БД дублируется логикой в backend/src/main.cpp (idempotent).

USE clipstream;

ALTER TABLE clips ADD COLUMN twitch_created_ts DATETIME GENERATED ALWAYS AS (
    STR_TO_DATE(NULLIF(TRIM(twitch_created_at),''), '%Y-%m-%dT%H:%i:%sZ')) STORED;

ALTER TABLE clips ADD INDEX idx_clips_feed_ts (twitch_created_ts, view_count);

ALTER TABLE view_history ADD INDEX idx_vh_user_clip (user_id, clip_id);
