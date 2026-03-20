USE clipstream;

ALTER TABLE clips ADD COLUMN clip_creator_twitch_id VARCHAR(64) NOT NULL DEFAULT '';
ALTER TABLE clips ADD COLUMN clip_creator_login VARCHAR(128) NOT NULL DEFAULT '';
ALTER TABLE clips ADD INDEX idx_clips_creator_tid (clip_creator_twitch_id);
