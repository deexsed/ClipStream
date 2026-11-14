import { useRef } from "react";
import { useRecommendationImpressions } from "../../shared/useRecommendationImpressions";
import { Link, useOutletContext } from "react-router-dom";
import {
  Eye,
  Twitch,
  Play,
  ThumbsUp,
  ThumbsDown,
  MessageCircle,
} from "lucide-react";
import {
  number,
  duration,
  twitchProfilePath,
  relativeTime,
  date,
} from "../../shared/format";
import { Avatar } from "../../shared/ui/Primitives";
import { useSession } from "../auth/Session";
import useClipVote from "./useClipVote";

export default function ClipCard({
  clip,
  featured = false,
  rank,
  showStats = false,
  priority = false,
}) {
  const impressionRef = useRef(null);
  useRecommendationImpressions(impressionRef, clip.delivery_id);
  const published = clip.twitch_created_at || clip.created_at;
  const channelPath = twitchProfilePath(
    clip.broadcaster_id,
    clip.broadcaster_login,
  );
  return (
    <article
      ref={impressionRef}
      data-recommendation-delivery={clip.delivery_id}
      className={`clip-card ${featured ? "clip-card-featured" : ""} ${showStats ? "overview-card" : ""}`}
    >
      <Link
        className="thumbnail"
        to={`/clip/${clip.id}`}
        aria-label={`Смотреть: ${clip.title}`}
      >
        {clip.thumbnail_url ? (
          <img
            src={clip.thumbnail_url}
            alt=""
            loading={featured || priority ? "eager" : "lazy"}
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
            {showStats && clip.game_name && (
              <Link
                className="overview-card-category"
                to={`/category/${clip.game_id}`}
                title={clip.game_name}
                aria-label={`Категория: ${clip.game_name}`}
              >
                {clip.game_name}
              </Link>
            )}
            {showStats && published && (
              <span
                className="overview-card-date"
                title={date(published, true)}
              >
                · {relativeTime(published)}
              </span>
            )}
            {!showStats && (
              <span>
                <Eye size={12} /> {number(clip.view_count)}
              </span>
            )}
          </p>
          {!showStats && (
            <p className="card-category">
              {clip.game_name && (
                <Link to={`/category/${clip.game_id}`}>{clip.game_name}</Link>
              )}
              {clip.language && <span>{clip.language.toUpperCase()}</span>}
            </p>
          )}
          {showStats && (
            <div className="overview-card-summary">
              <div className="overview-card-views">
                <span
                  title={`Просмотры на Twitch: ${clip.view_count || 0}`}
                  aria-label={`Просмотры на Twitch: ${clip.view_count || 0}`}
                >
                  <Twitch size={14} aria-hidden="true" />
                  {number(clip.view_count)}
                </span>
                {clip.platform_view_count != null && (
                  <span
                    title={`Просмотры в ClipStream: ${clip.platform_view_count}`}
                    aria-label={`Просмотры в ClipStream: ${clip.platform_view_count}`}
                  >
                    <img
                      src={`${import.meta.env.BASE_URL}favicon-16.png`}
                      width="16"
                      height="16"
                      alt=""
                    />
                    {number(clip.platform_view_count)}
                  </span>
                )}
              </div>
              <div
                className="overview-card-reactions"
                aria-label="Реакции и обсуждение"
              >
                <CardVotes clip={clip} />
                {clip.comment_count != null && (
                  <Link
                    to={`/clip/${clip.id}#comments`}
                    title="Открыть обсуждение клипа"
                    aria-label={`Комментарии в ClipStream: ${clip.comment_count}`}
                  >
                    <MessageCircle size={15} aria-hidden="true" />
                    {number(clip.comment_count)}
                  </Link>
                )}
              </div>
            </div>
          )}
        </div>
      </div>
    </article>
  );
}

function CardVotes({ clip }) {
  const session = useSession();
  const outlet = useOutletContext();
  const vote = useClipVote(clip.id);
  const react = (isLike) => {
    if (!session?.user) outlet?.openLogin();
    else if (!vote.isPending) vote.mutate(isLike);
  };
  return (
    <>
      <button
        type="button"
        title="Нравится"
        aria-label={`Лайки в ClipStream: ${clip.like_count || 0}`}
        aria-pressed={clip.user_vote === "like"}
        disabled={vote.isPending}
        onClick={() => react(true)}
      >
        <ThumbsUp size={14} />
        {number(clip.like_count)}
      </button>
      <button
        type="button"
        title="Не нравится"
        aria-label={`Дизлайки в ClipStream: ${clip.dislike_count || 0}`}
        aria-pressed={clip.user_vote === "dislike"}
        disabled={vote.isPending}
        onClick={() => react(false)}
      >
        <ThumbsDown size={14} />
        {number(clip.dislike_count)}
      </button>
      {vote.error && (
        <span className="overview-vote-error" role="alert">
          Не удалось сохранить голос. Попробуйте ещё раз.
        </span>
      )}
    </>
  );
}
