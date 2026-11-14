import { useRecommendationFeed } from "../../shared/api/recommendations";
import { useRecommendationImpressions } from "../../shared/useRecommendationImpressions";
import Select from "../../shared/ui/Select";
import Reveal from "../../shared/ui/Reveal";
import { useEffect, useRef, useState } from "react";
import { Link, useOutletContext } from "react-router-dom";
import { useMutation } from "@tanstack/react-query";
import {
  ArrowDown,
  ArrowUp,
  ArrowUpRight,
  MessageCircle,
  Share2,
  ThumbsUp,
  Zap,
  X,
} from "lucide-react";
import {
  useApi,
  usePages,
  pageItems,
  queryClient,
} from "../../shared/api/queries";
import { request } from "../../shared/api/http";
import { usePreferences } from "../../shared/preferences";
import {
  Avatar,
  Button,
  Dialog,
  IconButton,
  Status,
} from "../../shared/ui/Primitives";
import { number, twitchProfilePath } from "../../shared/format";
import { useSession } from "../auth/Session";
import { Preroll } from "../ads/Advertisement";
import { TwitchClip } from "./ClipPage";
import Comments from "./Comments";

export default function ClipRushPage() {
  const { languageKey } = usePreferences();
  const { user } = useSession();
  const [sort, setSort] = useState("recommended");
  return (
    <section className="rush" aria-label="ClipRush">
      <header className="rush-heading">
        <h1>
          <Zap size={20} fill="currentColor" /> ClipRush
        </h1>
        <label>
          <span className="sr-only">Порядок ClipRush</span>
          <Select
            aria-label="Порядок ClipRush"
            value={sort}
            onChange={(event) => setSort(event.target.value)}
          >
            <option value="recommended">Для вас</option>
            <option value="popular">Популярные</option>
            <option value="recent">Сначала новые</option>
          </Select>
        </label>
      </header>
      <RushFeed
        key={`${languageKey}:${user?.id || "guest"}:${sort}`}
        languageKey={languageKey}
        sort={sort}
      />
    </section>
  );
}

function RushFeed({ languageKey, sort }) {
  const { user } = useSession();
  const recommendations = useRecommendationFeed({
    viewer: user?.id,
    languages: languageKey,
    surface: "rush",
    pageSize: 12,
    enabled: sort === "recommended",
  });
  const browse = usePages(
    sort === "recommended" ? null : "/clips",
    "clips",
    { period: "all", sort, clip_langs: languageKey },
    { pageSize: 12 },
  );
  const feed = sort === "recommended" ? recommendations : browse;
  const rows = pageItems(feed, "clips");
  const rowKey = rows.map((row) => row.id).join(",");
  const scroller = useRef(null);
  const pendingMove = useRef(null);
  const seen = useRef(new Set());
  const [selected, setSelected] = useState(null);
  const active = rows.find((row) => row.id === selected) || rows[0];
  const activeId = useRef(active?.id);
  activeId.current = active?.id;
  const index = rows.findIndex((row) => row.id === active?.id);
  useRecommendationImpressions(scroller, `${rowKey}:${active?.id}`);
  const [dialog, setDialog] = useState(null);
  const [commentsOpen, setCommentsOpen] = useState(false);
  const [compact, setCompact] = useState(
    () => window.matchMedia("(max-width: 1023px)").matches,
  );
  const commentsPanel = useRef(null);
  const sheetDrag = useRef(null);
  useEffect(() => {
    if (commentsOpen && compact) return;
    const drag = sheetDrag.current;
    sheetDrag.current = null;
    if (drag) {
      drag.sheet.style.transition = "";
      if (drag.grip.hasPointerCapture(drag.pointerId))
        drag.grip.releasePointerCapture(drag.pointerId);
    }
  }, [commentsOpen, compact]);
  const resetSheet = (event, finish = false) => {
    const drag = sheetDrag.current;
    if (!drag || drag.pointerId !== event.pointerId) return;
    const sheet = event.currentTarget.closest("dialog");
    sheet.style.transition = "";
    sheetDrag.current = null;
    if (
      finish &&
      drag &&
      (drag.distance > 80 ||
        (drag.distance > 12 &&
          drag.distance / Math.max(1, performance.now() - drag.started) > 0.5))
    ) {
      closeComments();
    } else sheet.style.removeProperty("transform");
    if (event.currentTarget.hasPointerCapture(event.pointerId))
      event.currentTarget.releasePointerCapture(event.pointerId);
  };
  useEffect(() => {
    // Below 1024px, docking comments leaves less than Twitch's 400px minimum.
    const media = window.matchMedia("(max-width: 1023px)");
    const change = () => setCompact(media.matches);
    media.addEventListener?.("change", change);
    change();
    return () => media.removeEventListener?.("change", change);
  }, []);
  useEffect(() => {
    if (commentsOpen && !compact) commentsPanel.current?.focus();
  }, [commentsOpen, compact]);
  const closeComments = () => {
    setCommentsOpen(false);
    scroller.current?.focus();
  };
  useEffect(() => {
    const root = scroller.current;
    if (!root) return;
    const observer = new ResizeObserver(() => {
      root
        .querySelector(`[data-clip="${activeId.current}"]`)
        ?.scrollIntoView({ block: "start", behavior: "instant" });
    });
    observer.observe(root);
    return () => observer.disconnect();
  }, []);
  useEffect(() => {
    if (
      pendingMove.current &&
      scrollTo(pendingMove.current.id, pendingMove.current.smooth)
    )
      pendingMove.current = null;
  }, [rowKey]);
  useEffect(() => {
    const root = scroller.current;
    if (!root) return;
    const observer = new IntersectionObserver(
      (entries) => {
        const visible = entries
          .filter(
            (entry) =>
              entry.isIntersecting &&
              entry.intersectionRect.height /
                Math.min(entry.boundingClientRect.height, root.clientHeight) >=
                0.6,
          )
          .sort(
            (a, b) => b.intersectionRect.height - a.intersectionRect.height,
          )[0];
        if (visible) setSelected(Number(visible.target.dataset.clip));
      },
      { root, threshold: [0, 0.1, 0.3, 0.5, 0.6, 0.9] },
    );
    root
      .querySelectorAll("[data-clip]")
      .forEach((element) => observer.observe(element));
    return () => observer.disconnect();
  }, [rowKey]);
  useEffect(() => {
    if (active && !seen.current.has(active.id)) {
      seen.current.add(active.id);
      request(`/clips/${active.id}/view`, { method: "POST" }).catch(() => {});
    }
    if (
      index >= rows.length - 3 &&
      feed.hasNextPage &&
      !feed.isFetchingNextPage &&
      !feed.isFetchNextPageError
    )
      feed.fetchNextPage();
  }, [
    active?.id,
    index,
    rows.length,
    feed.hasNextPage,
    feed.isFetchingNextPage,
    feed.isFetchNextPageError,
  ]);
  const move = async (direction, smooth = false) => {
    let next = rows[index + direction];
    if (
      !next &&
      direction > 0 &&
      feed.hasNextPage &&
      !feed.isFetchingNextPage
    ) {
      const result = await feed.fetchNextPage();
      next = result.data?.pages.at(-1)?.clips[0];
      if (next && !scrollTo(next.id, smooth))
        pendingMove.current = { id: next.id, smooth };
    } else if (next) scrollTo(next.id, smooth);
  };
  const scrollTo = (id, smooth) => {
    const element = scroller.current?.querySelector(`[data-clip="${id}"]`);
    if (!element) return false;
    element.scrollIntoView({
      block: "start",
      behavior:
        smooth && !window.matchMedia("(prefers-reduced-motion: reduce)").matches
          ? "smooth"
          : "instant",
    });
    setSelected(id);
    return true;
  };
  return (
    <div
      className={`rush-body ${commentsOpen && !compact ? "has-comments" : ""}`}
    >
      <div
        className="rush-scroll"
        ref={scroller}
        tabIndex={0}
        aria-label="Вертикальная лента клипов"
        onKeyDown={(event) => {
          if (
            dialog ||
            event.target.closest("button,a,input,textarea,select,dialog") ||
            event.altKey ||
            event.ctrlKey ||
            event.metaKey
          )
            return;
          if (
            ["ArrowDown", "PageDown", "ArrowUp", "PageUp"].includes(event.key)
          ) {
            event.preventDefault();
            move(["ArrowDown", "PageDown"].includes(event.key) ? 1 : -1);
          }
        }}
      >
        {!rows.length && (
          <div className="rush-status">
            <Status
              loading={feed.isPending}
              error={feed.error}
              onRetry={feed.refetch}
              empty={
                !feed.isPending &&
                !feed.error &&
                "В этой подборке пока нет клипов"
              }
            />
          </div>
        )}
        {rows.map((clip, position) => (
          <article
            key={clip.id}
            data-clip={clip.id}
            data-recommendation-delivery={clip.delivery_id}
            data-recommendation-active={
              clip.id === active?.id ? "true" : "false"
            }
            className="rush-slide"
            aria-label={`Клип ${position + 1}: ${clip.title}`}
          >
            {clip.id === active?.id ? (
              <RushClip
                key={clip.id}
                clipId={clip.id}
                onDialog={(value) =>
                  value.type === "comments"
                    ? setCommentsOpen(true)
                    : setDialog(value)
                }
              />
            ) : (
              <div className="rush-card rush-idle" aria-hidden="true">
                <img src={clip.thumbnail_url} alt="" loading="lazy" />
                <p>{clip.title}</p>
              </div>
            )}
          </article>
        ))}
        {rows.length > 0 &&
          (feed.isFetchingNextPage ||
            feed.isFetchNextPageError ||
            !feed.hasNextPage) && (
            <div className="rush-end">
              <Status
                loading={feed.isFetchingNextPage}
                error={feed.isFetchNextPageError && feed.error}
                onRetry={feed.fetchNextPage}
              />
              {!feed.hasNextPage && !feed.isFetchingNextPage && (
                <div>
                  <p>Вы посмотрели эту подборку.</p>
                  {sort === "recommended" ? (
                    <Button onClick={feed.startNewSession}>
                      Новая подборка
                    </Button>
                  ) : (
                    <p>Выберите другой порядок или языки.</p>
                  )}
                </div>
              )}
            </div>
          )}
      </div>
      {!!rows.length && (
        <nav className="rush-navigation" aria-label="Переход между клипами">
          <IconButton
            label="Предыдущий клип"
            disabled={index <= 0}
            onClick={() => move(-1, true)}
          >
            <ArrowUp />
          </IconButton>
          <IconButton
            label="Следующий клип"
            disabled={
              index === rows.length - 1 &&
              (!feed.hasNextPage || feed.isFetchingNextPage)
            }
            onClick={() => move(1, true)}
          >
            <ArrowDown />
          </IconButton>
        </nav>
      )}
      <Reveal
        open={commentsOpen && !compact && !!active}
        as="aside"
        motion="panel"
        className="rush-comments"
        aria-label="Комментарии клипа"
        tabIndex={-1}
        ref={commentsPanel}
        onKeyDown={(event) => {
          if (event.key === "Escape") closeComments();
        }}
      >
        {active && (
          <div className="rush-comments-content">
            <div className="rush-comments-heading">
              <h2>Комментарии</h2>
              <IconButton label="Закрыть обсуждение" onClick={closeComments}>
                <X size={20} />
              </IconButton>
            </div>
            <p className="muted rush-comment-title">{active.title}</p>
            <Comments key={active.id} clipId={active.id} />
          </div>
        )}
      </Reveal>
      <Dialog
        open={commentsOpen && compact && !!active}
        onClose={closeComments}
        onAfterClose={() => {
          if (commentsOpen && !compact) commentsPanel.current?.focus();
          else if (!commentsOpen && document.activeElement === document.body)
            scroller.current?.focus();
        }}
        title="Комментарии"
        className="rush-comments-sheet"
      >
        <div
          className="rush-sheet-grip"
          aria-hidden="true"
          onPointerDown={(event) => {
            if (sheetDrag.current || !event.isPrimary || event.button !== 0)
              return;
            const sheet = event.currentTarget.closest("dialog");
            // Catch an entering sheet at its current position.
            const transform = getComputedStyle(sheet).transform;
            const offset =
              transform === "none" ? 0 : new DOMMatrixReadOnly(transform).m42;
            sheet.style.transform = `translateY(${offset}px)`;
            sheet.getAnimations().forEach((animation) => animation.cancel());
            sheetDrag.current = {
              start: event.clientY - offset,
              distance: offset,
              started: performance.now(),
              pointerId: event.pointerId,
              sheet,
              grip: event.currentTarget,
            };
            event.currentTarget.setPointerCapture(event.pointerId);
            event.currentTarget.closest("dialog").style.transition = "none";
          }}
          onPointerMove={(event) => {
            if (
              !sheetDrag.current ||
              sheetDrag.current.pointerId !== event.pointerId
            )
              return;
            sheetDrag.current.distance = Math.max(
              0,
              event.clientY - sheetDrag.current.start,
            );
            event.currentTarget.closest("dialog").style.transform =
              `translateY(${sheetDrag.current.distance}px)`;
          }}
          onPointerUp={(event) => resetSheet(event, true)}
          onPointerCancel={(event) => resetSheet(event)}
          onLostPointerCapture={(event) => resetSheet(event)}
        />
        {commentsOpen && compact && active && (
          <Comments key={active.id} clipId={active.id} />
        )}
      </Dialog>
      <Dialog
        open={!!dialog}
        onClose={() => setDialog(null)}
        title="Поделиться клипом"
        className="dialog-wide"
      >
        {dialog && (
          <label className="form-stack">
            Ссылка на клип
            <input
              readOnly
              value={`${window.location.origin}/clip/${dialog.id}`}
              onFocus={(event) => event.target.select()}
            />
            <a className="text-link" href={`/clip/${dialog.id}`}>
              Открыть страницу клипа <ArrowUpRight size={16} />
            </a>
          </label>
        )}
      </Dialog>
    </div>
  );
}

function RushClip({ clipId, onDialog }) {
  const { openLogin } = useOutletContext();
  const { user } = useSession();
  const detail = useApi(`/clips/${clipId}`);
  const clip = detail.data;
  const vote = useMutation({
    mutationFn: () =>
      request(`/clips/${clipId}/like`, {
        method: "POST",
        body: { is_like: true },
      }),
    onSuccess: (value) =>
      queryClient.setQueryData(["api", `/clips/${clipId}`], (previous) => ({
        ...previous,
        ...value,
      })),
  });
  return (
    <div className="rush-card">
      <div className="rush-media">
        <Status
          loading={detail.isPending}
          error={detail.error}
          onRetry={detail.refetch}
        >
          {clip && (
            <Preroll key={clipId} clipId={clipId}>
              <TwitchClip clip={clip} autoplay muted />
            </Preroll>
          )}
        </Status>
      </div>
      {clip && (
        <footer className="rush-footer">
          <Link
            className="rush-author"
            to={twitchProfilePath(clip.broadcaster_id, clip.broadcaster_login)}
          >
            <Avatar
              user={{
                username: clip.broadcaster_name,
                avatar_url: clip.broadcaster_avatar,
              }}
            />
            <strong>{clip.broadcaster_name}</strong>
            <ArrowUpRight size={16} />
          </Link>
          <h2>{clip.title}</h2>
          <div className="rush-meta">
            {clip.game_name && (
              <Link to={`/category/${clip.game_id}`}>{clip.game_name}</Link>
            )}
            <span>{number(clip.view_count)} просмотров Twitch</span>
          </div>
          <div className="rush-actions">
            <Button
              aria-pressed={clip.user_vote === "like"}
              busy={vote.isPending}
              onClick={() => (user ? vote.mutate() : openLogin())}
            >
              <ThumbsUp size={19} />
              {number(clip.like_count || 0)}
            </Button>
            <Button onClick={() => onDialog({ type: "comments", id: clipId })}>
              <MessageCircle size={19} />
              Обсудить
            </Button>
            <IconButton
              label="Поделиться клипом"
              onClick={() => onDialog({ type: "share", id: clipId })}
            >
              <Share2 size={20} />
            </IconButton>
            <Link className="text-link" to={`/clip/${clipId}`}>
              Подробнее <ArrowUpRight size={15} />
            </Link>
          </div>
          {vote.error && <Status error={vote.error} />}
        </footer>
      )}
    </div>
  );
}
