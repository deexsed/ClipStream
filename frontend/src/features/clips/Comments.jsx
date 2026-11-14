import Select from "../../shared/ui/Select";
import Reveal from "../../shared/ui/Reveal";
import { useId, useLayoutEffect, useRef, useState } from "react";
import { Link, useLocation, useOutletContext } from "react-router-dom";
import { useMutation } from "@tanstack/react-query";
import {
  ArrowUp,
  CornerDownRight,
  MessageCircle,
  ShieldCheck,
  ThumbsDown,
  ThumbsUp,
  X,
} from "lucide-react";
import { request } from "../../shared/api/http";
import { usePages, pageItems, queryClient } from "../../shared/api/queries";
import {
  Avatar,
  Button,
  IconButton,
  LoadMore,
  Status,
} from "../../shared/ui/Primitives";
import { date, relativeTime } from "../../shared/format";
import { useSession } from "../auth/Session";
import "./Comments.css";

export default function Comments({ clipId, anchor = false }) {
  const section = useRef(null);
  const { hash } = useLocation();
  useLayoutEffect(() => {
    if (!anchor || hash !== "#comments") return;
    section.current?.scrollIntoView({ block: "start", behavior: "instant" });
    section.current?.focus({ preventScroll: true });
  }, [clipId, hash, anchor]);
  const [sort, setSort] = useState("recent");
  const [reply, setReply] = useState(null);
  const [text, setText] = useState("");
  const [focused, setFocused] = useState(false);
  const [sent, setSent] = useState(false);
  const input = useRef(null);
  const inputId = useId();
  const composing = focused || Boolean(text) || Boolean(reply);
  useLayoutEffect(() => {
    if (input.current) {
      input.current.style.height = "auto";
      input.current.style.height = `${Math.min(160, Math.max(48, input.current.scrollHeight))}px`;
    }
  }, [text, composing]);
  const session = useSession();
  const { openLogin } = useOutletContext();
  const query = usePages(
    `/clips/${clipId}/comments`,
    "comments",
    { sort },
    { pageSize: 30 },
  );
  const rows = pageItems(query, "comments");
  const refresh = () =>
    queryClient.invalidateQueries({
      queryKey: ["pages", `/clips/${clipId}/comments`],
    });
  const submit = useMutation({
    mutationFn: () =>
      request(`/clips/${clipId}/comments`, {
        method: "POST",
        body: { body: text.trim(), ...(reply ? { parent_id: reply.id } : {}) },
      }),
    onSuccess: () => {
      setText("");
      setReply(null);
      setFocused(false);
      setSent(true);
      input.current?.blur();
      refresh();
    },
  });
  return (
    <section
      className={`comments ${anchor && hash === "#comments" ? "comments-target" : ""}`}
      id={anchor ? "comments" : undefined}
      ref={section}
      tabIndex={-1}
      aria-label="Обсуждение клипа"
    >
      <div className="comments-toolbar">
        <h2>
          <MessageCircle size={18} /> Обсуждение
        </h2>
        <Select
          aria-label="Порядок комментариев"
          value={sort}
          onChange={(event) => setSort(event.target.value)}
        >
          <option value="recent">Сначала новые</option>
          <option value="best">Лучшие</option>
        </Select>
      </div>
      {session.user ? (
        <form
          className={`comment-composer ${composing ? "is-active" : ""}`}
          onSubmit={(event) => {
            event.preventDefault();
            if (text.trim() && !submit.isPending) submit.mutate();
          }}
        >
          <Avatar user={session.user} />
          <div className="composer-main">
            <Reveal open={Boolean(reply)}>
              {reply && (
                <div className="composer-reply">
                  <CornerDownRight size={14} />
                  <span>
                    Ответ <strong>@{reply.author_name}</strong>
                  </span>
                  <IconButton
                    type="button"
                    label="Отменить ответ"
                    onClick={() => setReply(null)}
                  >
                    <X size={14} />
                  </IconButton>
                </div>
              )}
            </Reveal>
            <label className="sr-only" htmlFor={inputId}>
              Ваш комментарий
            </label>
            <textarea
              id={inputId}
              ref={input}
              rows={1}
              value={text}
              onChange={(event) => {
                setText(event.target.value);
                setSent(false);
              }}
              onFocus={() => setFocused(true)}
              onKeyDown={(event) => {
                if (event.key === "Enter" && (event.ctrlKey || event.metaKey)) {
                  event.preventDefault();
                  if (text.trim() && !submit.isPending) submit.mutate();
                }
              }}
              required
              minLength={1}
              maxLength={4000}
              placeholder={reply ? "Написать ответ…" : "Добавить комментарий…"}
            />
            <Reveal open={composing}>
              <div className="composer-controls">
                <span className="composer-count">
                  {text.length > 3500 ? `${text.length}/4000` : ""}
                </span>
                <Button
                  type="button"
                  className="composer-cancel"
                  onClick={() => {
                    setText("");
                    setReply(null);
                    setFocused(false);
                    input.current?.blur();
                  }}
                >
                  Отмена
                </Button>
                <Button
                  variant="primary"
                  busy={submit.isPending}
                  disabled={!text.trim()}
                >
                  <ArrowUp size={16} /> Отправить
                </Button>
              </div>
            </Reveal>
            {submit.error && <Status error={submit.error} />}
          </div>
        </form>
      ) : (
        <div className="comment-signin">
          <MessageCircle size={19} />
          <p>Поделитесь своим мнением</p>
          <Button onClick={openLogin}>Войти через Twitch</Button>
        </div>
      )}
      {sent && (
        <span className="feedback composer-feedback" role="status">
          Комментарий отправлен
        </span>
      )}
      <div className="comment-stream">
        <Status
          error={!query.data && query.error}
          loading={query.isPending}
          onRetry={query.refetch}
        >
          {rows.length === 0 && (
            <div className="comments-empty">
              <span className="comments-empty-icon">
                <MessageCircle size={23} strokeWidth={1.5} />
              </span>
              <strong>Пока без комментариев</strong>
              <p>Начните обсуждение этого момента.</p>
            </div>
          )}
          <div className="comment-list">
            {rows.map((row) => (
              <Comment
                key={row.id}
                comment={row}
                clipId={clipId}
                onReply={(value) => {
                  if (session.user) {
                    setReply(value);
                    setFocused(true);
                    input.current?.focus();
                  } else openLogin();
                }}
                refresh={refresh}
              />
            ))}
          </div>
          <LoadMore query={query} />
        </Status>
      </div>
    </section>
  );
}

function Comment({ comment, onReply, refresh, depth = 0, clipId }) {
  const session = useSession();
  const { openLogin } = useOutletContext();
  const [editing, setEditing] = useState(false);
  const [text, setText] = useState(comment.body);
  const [confirmDelete, setConfirmDelete] = useState(false);
  const [expanded, setExpanded] = useState(false);
  const children = usePages(
    expanded && clipId ? `/clips/${clipId}/comments` : null,
    "comments",
    { parent_id: comment.id },
    { pageSize: 30 },
  );
  const change = useMutation({
    mutationFn: ({ method, suffix = "", body }) =>
      request(`/comments/${comment.id}${suffix}`, { method, body }),
    onSuccess: () => {
      setEditing(false);
      setConfirmDelete(false);
      refresh();
    },
  });
  const canEdit = session.user?.id === comment.user_id;
  const canDelete = canEdit || [1, 2].includes(session.user?.role);
  const vote = (value) => {
    if (!session.user) openLogin();
    else change.mutate({ method: "POST", suffix: "/vote", body: { value } });
  };
  return (
    <article
      className={`comment ${depth ? "comment-reply" : ""} ${depth >= 2 ? "comment-flat" : ""}`}
    >
      {depth < 2 && (
        <Avatar
          user={{
            username: comment.author_name,
            avatar_url: comment.author_avatar,
          }}
        />
      )}
      <div className="comment-content">
        <div className="comment-author">
          <Link to={`/u/${encodeURIComponent(comment.author_name)}`}>
            {comment.author_name}
          </Link>
          {comment.is_clip_channel_owner && (
            <span className="comment-badge">Автор канала</span>
          )}
          {["admin", "moderator"].includes(comment.author_role) && (
            <span
              className="comment-staff"
              title={
                comment.author_role === "admin" ? "Администратор" : "Модератор"
              }
            >
              <ShieldCheck size={13} />
            </span>
          )}
          <time title={date(comment.created_at, true)}>
            {relativeTime(comment.created_at)}
          </time>
          {comment.updated_at && comment.updated_at !== comment.created_at && (
            <span className="comment-edited">изменено</span>
          )}
        </div>
        <Reveal open={editing}>
          <form
            className="form-stack comment-editor"
            onSubmit={(event) => {
              event.preventDefault();
              change.mutate({ method: "PUT", body: { body: text.trim() } });
            }}
          >
            <textarea
              aria-label="Изменить комментарий"
              value={text}
              onChange={(event) => setText(event.target.value)}
              maxLength={4000}
              required
              autoFocus
            />
            <div className="button-row">
              <Button busy={change.isPending}>Сохранить</Button>
              <Button type="button" onClick={() => setEditing(false)}>
                Отмена
              </Button>
            </div>
          </form>
        </Reveal>
        {!editing && <p className="comment-body">{comment.body}</p>}
        <div className="comment-actions">
          <button
            aria-label="Полезный комментарий"
            aria-pressed={comment.user_vote === 1}
            disabled={change.isPending}
            onClick={() => vote(comment.user_vote === 1 ? 0 : 1)}
          >
            <ThumbsUp size={15} />
            {comment.upvotes > 0 && comment.upvotes}
          </button>
          <button
            aria-label="Неполезный комментарий"
            aria-pressed={comment.user_vote === -1}
            disabled={change.isPending}
            onClick={() => vote(comment.user_vote === -1 ? 0 : -1)}
          >
            <ThumbsDown size={15} />
            {comment.downvotes > 0 && comment.downvotes}
          </button>
          {depth < 7 && (
            <button onClick={() => onReply(comment)}>Ответить</button>
          )}
          {canEdit && (
            <button
              onClick={() => {
                setText(comment.body);
                setEditing(true);
              }}
            >
              Изменить
            </button>
          )}
          {canDelete && (
            <button onClick={() => setConfirmDelete(true)}>Удалить</button>
          )}
        </div>
        <Reveal open={confirmDelete}>
          <div className="confirmation" role="alert">
            Удалить комментарий и ответы?
            <Button
              busy={change.isPending}
              onClick={() => change.mutate({ method: "DELETE" })}
            >
              Удалить
            </Button>
            <Button onClick={() => setConfirmDelete(false)}>Отмена</Button>
          </div>
        </Reveal>
        {change.error && <Status error={change.error} />}
        {(expanded
          ? pageItems(children, "comments")
          : comment.replies || []
        ).map((reply) => (
          <Comment
            key={reply.id}
            comment={reply}
            clipId={clipId}
            refresh={refresh}
            onReply={onReply}
            depth={depth + 1}
          />
        ))}
        {comment.replies_truncated && !expanded && (
          <Button onClick={() => setExpanded(true)}>
            Все ответы ({comment.total_replies})
          </Button>
        )}
        {expanded && (
          <>
            <Status
              loading={children.isPending}
              error={!children.data && children.error}
              onRetry={children.refetch}
            />
            <LoadMore query={children} />
          </>
        )}
      </div>
    </article>
  );
}
