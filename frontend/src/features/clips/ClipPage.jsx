import { recordCategoryUse } from "../../shared/categoryHistory";
import ShareDialog from "./ShareDialog";
import ClipVoteButtons from "./ClipVoteButtons";
import TwitchImage from "../../shared/ui/TwitchImage";
import ExternalLink from "../../shared/ui/ExternalLink";
import { SectionHeading } from "../../shared/ui/Layout";
import { Feedback } from "../../shared/ui/Feedback";
import { TextArea, Field } from "../../shared/ui/FormControls";
import { useEffect, useRef, useState } from "react";
import { Link, useOutletContext, useParams } from "react-router-dom";
import { useMutation } from "@tanstack/react-query";
import useClipVote from "./useClipVote";
import {
  ArrowLeft,
  ArrowUpRight,
  Eye,
  Flag,
  Link2,
  MoreHorizontal,
  Play,
} from "lucide-react";
import { request, queryString } from "../../shared/api/http";
import { useApi, queryClient } from "../../shared/api/queries";
import { usePreferences } from "../../shared/preferences";
import {
  Identity,
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

export default function ClipPage() {
  const { id } = useParams();
  const { openLogin } = useOutletContext();
  const { languageKey } = usePreferences();
  const session = useSession();
  const validId = /^\d+$/.test(id || "");
  const clipQuery = useApi(validId ? `/clips/${id}` : null, {
    staleTime: 60000,
  });
  const clip = clipQuery.data;
  const related = useApi(
    validId
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
      recordCategoryUse(clip.game_id, session.user?.id, "views");
      request(`/clips/${clip.id}/view`, { method: "POST" }).catch(() => {});
    }
  }, [clip?.id]);
  const vote = useClipVote(id, openLogin);

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
  return (
    <div className="page watch-page">
      <div className="watch-topline">
        <Link className="back-link" to="/">
          <ArrowLeft size={17} /> Главная
        </Link>
      </div>
      <Status
        error={
          clipQuery.error ||
          (!validId
            ? new Error("Клип не найден. Выберите другой ролик.")
            : null)
        }
        loading={validId && clipQuery.isPending}
        onRetry={clipQuery.refetch}
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
                <Identity
                  user={{
                    username: clip.broadcaster_name,
                    avatar_url: clip.broadcaster_avatar,
                  }}
                >
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
                </Identity>
                <div className="reaction-buttons">
                  <ClipVoteButtons clip={clip} vote={vote} />

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
              {message && <Feedback role="status">{message}</Feedback>}
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
              <SectionHeading>
                <h2>Продолжить смотреть</h2>
                <Link to="/" aria-label="На главную">
                  <ArrowUpRight size={18} />
                </Link>
              </SectionHeading>
              <Status
                error={related.error}
                loading={related.isPending}
                onRetry={related.refetch}
              >
                {(related.data?.clips || []).map((row) => (
                  <ClipCard key={row.id} clip={row} />
                ))}
                {related.data?.clips?.length === 0 && (
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
      <ShareDialog
        open={action === "share"}
        onClose={() => setAction(null)}
        clipId={id}
      />
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
        <ExternalLink className="player-preview" href={url}>
          {clip.thumbnail_url && (
            <TwitchImage src={clip.thumbnail_url} alt="" decoding="async" />
          )}
          <span className="external-play">
            <Play size={22} fill="currentColor" /> Смотреть на Twitch{" "}
            <ArrowUpRight size={16} />
          </span>
        </ExternalLink>
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
          <Field>
            Сообщить о нарушении
            <TextArea
              required
              minLength={3}
              maxLength={2000}
              value={reason}
              onChange={(event) => setReason(event.target.value)}
              placeholder="Что случилось?"
            />
          </Field>
          <Button busy={mutation.isPending}>
            <Flag size={15} /> Отправить жалобу
          </Button>
        </form>
      </div>
    </Dialog>
  );
}
