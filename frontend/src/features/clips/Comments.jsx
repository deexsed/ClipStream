import { useState } from "react";
import { Link, useOutletContext } from "react-router-dom";
import { useMutation } from "@tanstack/react-query";
import { MessageCircle, ThumbsDown, ThumbsUp } from "lucide-react";
import { request } from "../../shared/api/http";
import { usePages, pageItems, queryClient } from "../../shared/api/queries";
import { Avatar, Button, LoadMore, Status } from "../../shared/ui/Primitives";
import { date } from "../../shared/format";
import { useSession } from "../auth/Session";

export default function Comments({ clipId }) {
  const [sort, setSort] = useState("recent");
  const [reply, setReply] = useState(null);
  const [text, setText] = useState("");
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
      refresh();
    },
  });
  return (
    <section className="comments">
      <div className="section-label">
        <h2>
          <MessageCircle size={18} /> Обсуждение
        </h2>
        <label>
          <span className="sr-only">Порядок комментариев</span>
          <select
            value={sort}
            onChange={(event) => setSort(event.target.value)}
          >
            <option value="recent">Сначала новые</option>
            <option value="best">Лучшие</option>
          </select>
        </label>
      </div>
      {session.user ? (
        <form
          className="comment-form"
          onSubmit={(event) => {
            event.preventDefault();
            if (text.trim()) submit.mutate();
          }}
        >
          {reply && (
            <div className="reply-label">
              Ответ пользователю {reply.author_name}
              <button type="button" onClick={() => setReply(null)}>
                Отменить
              </button>
            </div>
          )}
          <label>
            <span className="sr-only">Ваш комментарий</span>
            <textarea
              value={text}
              onChange={(event) => setText(event.target.value)}
              required
              minLength={1}
              maxLength={4000}
              placeholder={
                reply ? "Ваш ответ…" : "Что думаете об этом моменте?"
              }
            />
          </label>
          <div className="comment-submit">
            <span className="muted">Обсуждайте момент, уважайте людей.</span>
            <Button
              variant="primary"
              busy={submit.isPending}
              disabled={!text.trim()}
            >
              Отправить
            </Button>
          </div>
          {submit.error && <Status error={submit.error} />}
        </form>
      ) : (
        <div className="signin-note">
          <p>Присоединяйтесь к обсуждению</p>
          <Button onClick={openLogin}>Войти, чтобы ответить</Button>
        </div>
      )}
      <Status
        error={!query.data && query.error}
        loading={query.isPending}
        onRetry={query.refetch}
      >
        {rows.length === 0 && (
          <p className="muted empty-comment">
            Пока тихо. Начните разговор первым.
          </p>
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
                  document.querySelector(".comment-form textarea")?.focus();
                } else openLogin();
              }}
              refresh={refresh}
            />
          ))}
        </div>
        <LoadMore query={query} />
      </Status>
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
    <article className={`comment ${depth ? "comment-reply" : ""}`}>
      <Avatar
        user={{
          username: comment.author_name,
          avatar_url: comment.author_avatar,
        }}
      />
      <div className="comment-content">
        <div className="comment-author">
          <Link to={`/u/${encodeURIComponent(comment.author_name)}`}>
            {comment.author_name}
          </Link>
          <time>{date(comment.created_at)}</time>
        </div>
        {editing ? (
          <form
            className="form-stack"
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
            />
            <div className="button-row">
              <Button busy={change.isPending}>Сохранить</Button>
              <Button type="button" onClick={() => setEditing(false)}>
                Отмена
              </Button>
            </div>
          </form>
        ) : (
          <p className="comment-body">{comment.body}</p>
        )}
        <div className="comment-actions">
          <button
            aria-label="Полезный комментарий"
            aria-pressed={comment.user_vote === 1}
            disabled={change.isPending}
            onClick={() => vote(comment.user_vote === 1 ? 0 : 1)}
          >
            <ThumbsUp size={13} />
            {comment.upvotes || 0}
          </button>
          <button
            aria-label="Неполезный комментарий"
            aria-pressed={comment.user_vote === -1}
            disabled={change.isPending}
            onClick={() => vote(comment.user_vote === -1 ? 0 : -1)}
          >
            <ThumbsDown size={13} />
            {comment.downvotes || 0}
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
        {confirmDelete && (
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
        )}
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
            depth={Math.min(depth + 1, 3)}
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
