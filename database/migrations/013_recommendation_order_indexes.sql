-- Match the complete descending order, including deterministic ID tie-breaks.
-- An implicit ascending primary-key suffix cannot satisfy these mixed indexes.
ALTER TABLE clips DROP INDEX idx_clips_views, ADD INDEX idx_clips_views(view_count DESC,id DESC);
ALTER TABLE clips ADD INDEX idx_clips_recent_order(twitch_created_ts DESC,id DESC);
ALTER TABLE recommendation_clip_features DROP INDEX idx_rcf_audience, ADD INDEX idx_rcf_audience(audience_score DESC,clip_id DESC);
ALTER TABLE recommendation_predictions DROP INDEX idx_rp_score, ADD INDEX idx_rp_score(user_id,score DESC,clip_id DESC);
