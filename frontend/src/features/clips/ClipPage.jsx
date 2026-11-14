import { useEffect, useRef, useState } from "react";
import {
  Link,
  useOutletContext,
  useParams,
  useSearchParams,
} from "react-router-dom";
import { useMutation } from "@tanstack/react-query";
import useClipVote from "./useClipVote";
import {
  ArrowDown,
  ArrowLeft,
  ArrowUp,
  ArrowUpRight,
  Eye,
  Flag,
  Link2,
  MoreHorizontal,
  Play,
  ThumbsDown,
  ThumbsUp,
} from "lucide-react";
import { request, queryString } from "../../shared/api/http";
import {
  useApi,
  usePages,
  pageItems,
  queryClient,
} from "../../shared/api/queries";
import { usePreferences } from "../../shared/preferences";
import {
  Avatar,
  Button,
  Dialog,
  IconButton,
  Status,
} from "../../shared/ui/Primitives";
import { date, duration, number, twitchProfilePath } from "../../shared/format";
import { useSession } from "../auth/Session";
import ClipCard from "./ClipCard";
import Comments from "./Comments";
import { Preroll } from "../ads/Advertisement";

export default function ClipPage({ feed = false }) {
  const { id: routeId } = useParams();
  const [params, setParams] = useSearchParams();
  const { openLogin } = useOutletContext();
  const { languageKey } = usePreferences();
  const session = useSession();
  const queue = usePages(feed ? "/clips" : null, "clips", {
    period: "all",
    sort: "popular",
    clip_langs: languageKey,
  });
  const rows = pageItems(queue, "clips");
  const id = feed ? params.get("start") || String(rows[0]?.id || "") : routeId;
  const validId = /^\d+$/.test(id || "");
  const clipQuery = useApi(validId ? `/clips/${id}` : null, {
    staleTime: 60000,
  });
  const clip = clipQuery.data;
  const related = useApi(
    !feed && validId
      ? `/clips/${id}/related?${queryString({ limit: 8, clip_langs: languageKey })}`
      : null,
  );
  const opened = useRef(null);
  const [action, setAction] = useState(null);
  const [message, setMessage] = useState("");
  useEffect(() => {
    setMessage("");
    setAction(null);
    if (clip?.id && opened.current !== clip.id) {
      opened.current = clip.id;
      request(`/clips/${clip.id}/view`, { method: "POST" }).catch(() => {});
    }
  }, [clip?.id]);
  const vote = useClipVote(id);
  const react = (isLike) => {
    if (!session.user) openLogin();
    else vote.mutate(isLike);
  };
  const index = rows.findIndex((row) => String(row.id) === id);
  const navigateClip = (next) => {
    if (next != null)
      setParams((previous) => {
        const updated = new URLSearchParams(previous);
        updated.set("start", String(next));
        return updated;
      });
  };
  useEffect(() => {
    if (!feed) return;
    const handler = (event) => {
      if (
        event.altKey ||
        event.ctrlKey ||
        event.metaKey ||
        event.target.closest(
          "input,textarea,select,button,a,[contenteditable],dialog",
        )
      )
        return;
      if (event.key === "ArrowDown" && rows[index + 1]) {
        event.preventDefault();
        navigateClip(rows[index + 1].id);
      }
      if (event.key === "ArrowUp" && rows[index - 1]) {
        event.preventDefault();
        navigateClip(rows[index - 1].id);
      }
    };
    window.addEventListener("keydown", handler);
    return () => window.removeEventListener("keydown", handler);
  }, [feed, id, rows]);
  const share = async () => {
    try {
      await navigator.clipboard.writeText(
        `${window.location.origin}/clip/${id}`,
      );
      setMessage("Ссылка скопирована");
    } catch {
      setAction("share");
    }
  };
  const emptyFeed =
    feed &&
    !params.get("start") &&
    !queue.isPending &&
    !queue.error &&
    rows.length === 0;
  const error =
    queue.error ||
    clipQuery.error ||
    (!validId && !emptyFeed && (!feed || !queue.isPending)
      ? new Error("Клип не найден. Выберите другой ролик.")
      : null);
  return (
    <div className={`page watch-page ${feed ? "watch-feed" : ""}`}>
      <div className="watch-topline">
        <Link className="back-link" to="/">
          <ArrowLeft size={17} /> Обзор
        </Link>
        {feed && (
          <div className="watch-navigation">
            <Button
              disabled={index <= 0}
              onClick={() => navigateClip(rows[index - 1]?.id)}
            >
              <ArrowUp size={16} /> Предыдущий
            </Button>
            <Button
              disabled={!rows[index + 1] && !queue.hasNextPage}
              busy={queue.isFetchingNextPage}
              onClick={async () => {
                if (rows[index + 1]) navigateClip(rows[index + 1].id);
                else {
                  const next = await queue.fetchNextPage();
                  const item = next.data?.pages.at(-1)?.clips[0];
                  if (item) navigateClip(item.id);
                }
              }}
            >
              Следующий <ArrowDown size={16} />
            </Button>
          </div>
        )}
      </div>
      <Status
        error={error}
        loading={(feed && queue.isPending) || (validId && clipQuery.isPending)}
        empty={emptyFeed && "Нет клипов для текущих настроек"}
        onRetry={() => (validId ? clipQuery.refetch() : queue.refetch())}
      >
        {clip && (
          <div className="watch-layout">
            <div className="watch-main">
              <Preroll key={clip.id} clipId={clip.id}>
                <TwitchClip clip={clip} />
              </Preroll>
              <div className="watch-title">
                <h1>{clip.title}</h1>
                <div className="watch-meta">
                  {clip.game_name && (
                    <Link to={`/category/${clip.game_id}`}>
                      {clip.game_name}
                    </Link>
                  )}
                  <span>
                    <Eye size={14} /> {number(clip.view_count)} просмотров
                    Twitch
                  </span>
                  <span>{date(clip.twitch_created_at || clip.created_at)}</span>
                  <span>{duration(clip.duration)}</span>
                  <span>{clip.language?.toUpperCase()}</span>
                </div>
              </div>
              <div className="watch-toolbar">
                <div className="creator">
                  <Avatar
                    user={{
                      username: clip.broadcaster_name,
                      avatar_url: clip.broadcaster_avatar,
                    }}
                  />
                  <div>
                    {twitchProfilePath(
                      clip.broadcaster_id,
                      clip.broadcaster_login,
                    ) ? (
                      <Link
                        to={twitchProfilePath(
                          clip.broadcaster_id,
                          clip.broadcaster_login,
                        )}
                      >
                        {clip.broadcaster_name}
                      </Link>
                    ) : (
                      <strong>{clip.broadcaster_name}</strong>
                    )}
                    <small>Канал на Twitch</small>
                  </div>
                </div>
                <div className="reaction-buttons">
                  <button
                    className={clip.user_vote === "like" ? "selected" : ""}
                    aria-label="Нравится"
                    aria-pressed={clip.user_vote === "like"}
                    disabled={vote.isPending}
                    onClick={() => react(true)}
                  >
                    <ThumbsUp size={17} /> {number(clip.like_count)}
                  </button>
                  <button
                    className={clip.user_vote === "dislike" ? "selected" : ""}
                    aria-label="Не нравится"
                    aria-pressed={clip.user_vote === "dislike"}
                    disabled={vote.isPending}
                    onClick={() => react(false)}
                  >
                    <ThumbsDown size={17} /> {number(clip.dislike_count)}
                  </button>
                  <IconButton label="Скопировать ссылку" onClick={share}>
                    <Link2 size={18} />
                  </IconButton>
                  <IconButton
                    label="Действия с клипом"
                    onClick={() =>
                      session.user ? setAction("options") : openLogin()
                    }
                  >
                    <MoreHorizontal size={19} />
                  </IconButton>
                </div>
              </div>
              {vote.error && <Status error={vote.error} />}
              {message && (
                <p role="status" className="feedback">
                  {message}
                </p>
              )}
              {clip.creator_twitch_id && (
                <p className="classification-note muted">
                  Клип создал{" "}
                  <Link
                    className="text-link"
                    to={twitchProfilePath(
                      clip.creator_twitch_id,
                      clip.creator_login,
                    )}
                  >
                    {clip.creator_login || "автор на Twitch"}
                  </Link>
                </p>
              )}
              {clip.tags?.length > 0 && (
                <nav className="clip-tags" aria-label="Теги клипа">
                  {clip.tags.map((tag) => (
                    <Link
                      key={tag}
                      to={`/?browse=1&tag=${encodeURIComponent(tag)}`}
                    >
                      #{tag}
                    </Link>
                  ))}
                </nav>
              )}
              <Comments clipId={clip.id} anchor />
            </div>
            <aside className="related">
              <div className="section-label">
                <h2>{feed ? "В этой подборке" : "Продолжить смотреть"}</h2>
                <Link
                  to={feed ? "/" : `/feed?start=${id}`}
                  aria-label="Открыть ленту"
                >
                  <ArrowUpRight size={18} />
                </Link>
              </div>
              <Status
                error={related.error}
                loading={!feed && related.isPending}
                onRetry={related.refetch}
              >
                {(feed
                  ? rows.filter((row) => row.id !== clip.id).slice(0, 8)
                  : related.data?.clips || []
                ).map((row) => (
                  <ClipCard key={row.id} clip={row} />
                ))}
                {!feed && related.data?.clips?.length === 0 && (
                  <p className="muted">
                    Похожие клипы пока не найдены. Загляните в{" "}
                    <Link to="/trends">тренды</Link>.
                  </p>
                )}
              </Status>
            </aside>
          </div>
        )}
      </Status>
      {emptyFeed && (
        <p className="muted">
          Проверьте выбранные языки
          {session.user ? (
            <>
              {" "}
              и{" "}
              <Link
                className="text-link"
                to={`/u/${encodeURIComponent(session.user.username)}?tab=settings`}
              >
                настройки ленты
              </Link>
              .
            </>
          ) : (
            ". Попробуйте другую подборку в обзоре."
          )}
        </p>
      )}
      <Dialog
        open={action === "share"}
        onClose={() => setAction(null)}
        title="Ссылка на клип"
      >
        <label className="form-stack">
          Скопируйте ссылку
          <input
            readOnly
            value={`${window.location.origin}/clip/${id}`}
            onFocus={(event) => event.target.select()}
          />
        </label>
      </Dialog>
      <ClipActions
        open={action === "options"}
        onClose={() => setAction(null)}
        clipId={id}
        onDone={setMessage}
      />
    </div>
  );
}

export function TwitchClip({ clip, autoplay = false, muted = false }) {
  const container = useRef(null);
  const [wideEnough, setWideEnough] = useState(false);
  useEffect(() => {
    const observer = new ResizeObserver(([entry]) =>
      setWideEnough(entry.contentRect.width >= 400),
    );
    if (container.current) observer.observe(container.current);
    return () => observer.disconnect();
  }, []);
  const url = `https://clips.twitch.tv/${encodeURIComponent(clip.twitch_clip_id || "")}`;
  const embed = `https://clips.twitch.tv/embed?${queryString({ clip: clip.twitch_clip_id, parent: window.location.hostname, autoplay, muted })}`;
  return (
    <div ref={container} className="player-container">
      {wideEnough && clip.twitch_clip_id ? (
        <iframe
          key={clip.id}
          src={embed}
          title={clip.title}
          allow="fullscreen; autoplay; picture-in-picture"
          allowFullScreen
          referrerPolicy="strict-origin-when-cross-origin"
        />
      ) : (
        <a
          className="player-preview"
          href={url}
          target="_blank"
          rel="noopener noreferrer"
        >
          {clip.thumbnail_url && <img src={clip.thumbnail_url} alt="" />}
          <span className="external-play">
            <Play size={22} fill="currentColor" /> Смотреть на Twitch{" "}
            <ArrowUpRight size={16} />
          </span>
        </a>
      )}
    </div>
  );
}

function ClipActions({ open, onClose, clipId, onDone }) {
  const [reason, setReason] = useState("");
  const mutation = useMutation({
    mutationFn: ({ path, body }) => request(path, { method: "POST", body }),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ["recommendations"] });
      onDone("Готово. Ваши настройки сохранены.");
      onClose();
    },
  });
  return (
    <Dialog open={open} onClose={onClose} title="Управление клипом">
      <div className="form-stack">
        {mutation.error && <Status error={mutation.error} />}
        {[
          ["not_interested", "Не рекомендовать этот клип"],
          ["mute_channel", "Скрыть канал из рекомендаций"],
          ["mute_game", "Скрыть категорию"],
        ].map(([action, label]) => (
          <Button
            key={action}
            busy={mutation.isPending}
            onClick={() =>
              mutation.mutate({
                path: `/clips/${clipId}/feedback`,
                body: { action },
              })
            }
          >
            {label}
          </Button>
        ))}
        <form
          className="form-stack"
          onSubmit={(event) => {
            event.preventDefault();
            mutation.mutate({
              path: `/clips/${clipId}/report`,
              body: { reason },
            });
          }}
        >
          <label>
            Сообщить о нарушении
            <textarea
              required
              minLength={3}
              maxLength={2000}
              value={reason}
              onChange={(event) => setReason(event.target.value)}
              placeholder="Что случилось?"
            />
          </label>
          <Button busy={mutation.isPending}>
            <Flag size={15} /> Отправить жалобу
          </Button>
        </form>
      </div>
    </Dialog>
  );
}
