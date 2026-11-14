import { Link } from "react-router-dom";
import { Eye, Play } from "lucide-react";
import { number, duration, twitchProfilePath } from "../../shared/format";
import { Avatar } from "../../shared/ui/Primitives";

export default function ClipCard({ clip, featured = false, rank }) {
  const channelPath = twitchProfilePath(
    clip.broadcaster_id,
    clip.broadcaster_login,
  );
  return (
    <article className={`clip-card ${featured ? "clip-card-featured" : ""}`}>
      <Link
        className="thumbnail"
        to={`/clip/${clip.id}`}
        aria-label={`Смотреть: ${clip.title}`}
      >
        {clip.thumbnail_url ? (
          <img
            src={clip.thumbnail_url}
            alt=""
            loading={featured ? "eager" : "lazy"}
            fetchPriority={featured ? "high" : "auto"}
          />
        ) : (
          <span className="thumbnail-fallback">
            <Play size={28} />
          </span>
        )}
        {rank && <span className="rank">{String(rank).padStart(2, "0")}</span>}
        {clip.promo_label && (
          <span className="promotion-label">
            Продвижение · {clip.promo_label}
          </span>
        )}
        <span className="clip-duration">{duration(clip.duration)}</span>
        <span className="play-affordance">
          <Play size={24} fill="currentColor" />
        </span>
      </Link>
      <div className="card-description">
        <Avatar
          user={{
            avatar_url: clip.broadcaster_avatar,
            username: clip.broadcaster_name,
          }}
        />
        <div>
          <Link className="clip-title" to={`/clip/${clip.id}`}>
            {clip.title}
          </Link>
          <p className="card-channel">
            {channelPath ? (
              <Link to={channelPath}>{clip.broadcaster_name}</Link>
            ) : (
              clip.broadcaster_name
            )}
            <span>
              <Eye size={12} /> {number(clip.view_count)}
            </span>
          </p>
          <p className="card-category">
            {clip.game_name && (
              <Link to={`/category/${clip.game_id}`}>{clip.game_name}</Link>
            )}
            {clip.language && <span>{clip.language.toUpperCase()}</span>}
          </p>
        </div>
      </div>
    </article>
  );
}
