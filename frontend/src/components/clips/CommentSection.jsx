import { useState, useEffect, useCallback } from 'react';
import { Link } from 'react-router-dom';
import {
  MessageSquare,
  ThumbsUp,
  ThumbsDown,
  Reply,
  Trash2,
  ChevronDown,
  UserPlus,
  UserMinus,
  Sparkles,
  Check,
  X,
} from 'lucide-react';
import { comments as commentsApi, social as socialApi } from '../../api/client';
import useAuthStore from '../../store/useAuthStore';

const BADGE_STYLES = {
  admin: { label: 'Админ', className: 'border-red-500/50 bg-red-500/15 text-red-300' },
  moderator: { label: 'Модератор', className: 'border-amber-500/50 bg-amber-500/15 text-amber-200' },
  vip: { label: 'VIP', className: 'border-fuchsia-500/50 bg-fuchsia-500/15 text-fuchsia-200' },
  broadcaster: { label: 'Стример', className: 'border-sky-500/50 bg-sky-500/15 text-sky-200' },
  legend: { label: 'Legend', className: 'border-yellow-500/50 bg-yellow-500/20 text-yellow-200' },
  pro: { label: 'Pro', className: 'border-cyan-500/40 bg-cyan-500/10 text-cyan-200' },
  rising_star: { label: 'Rising Star', className: 'border-violet-500/40 bg-violet-500/10 text-violet-200' },
};

function legacyRoleBadges(role) {
  if (role === 'admin') return ['admin'];
  if (role === 'moderator') return ['moderator'];
  if (role === 'vip') return ['vip'];
  return [];
}

function AuthorBadges({ comment }) {
  const raw = comment.author_badges?.length ? comment.author_badges : legacyRoleBadges(comment.author_role);
  return (
    <>
      {raw.map((b) => {
        const cfg = BADGE_STYLES[b];
        if (!cfg) return null;
        return (
          <span
            key={`${comment.id}-${b}`}
            className={`text-[10px] font-bold uppercase tracking-wide px-1.5 py-0.5 rounded border ${cfg.className}`}
          >
            {cfg.label}
          </span>
        );
      })}
    </>
  );
}

function nextVoteValue(current, direction) {
  if (direction === 'up') {
    if (current === 1) return 0;
    return 1;
  }
  if (current === -1) return 0;
  return -1;
}

function CommentItem({
  comment,
  onReply,
  onDelete,
  onVote,
  onFriendAction,
  currentUserId,
  canModerate,
  isAuth,
  followBusyId,
  compactActions,
}) {
  const [showReplies, setShowReplies] = useState(false);
  const canDelete = currentUserId === comment.user_id || canModerate;
  const up = Number(comment.upvotes) || 0;
  const down = Number(comment.downvotes) || 0;
  const my = comment.user_vote;
  const profilePath = comment.author_name ? `/u/${encodeURIComponent(comment.author_name)}` : null;
  const fs = comment.viewer_friendship || 'none';
  const showFriendRow =
    isAuth && currentUserId && comment.user_id !== currentUserId && fs !== 'self';
  const busy = followBusyId === comment.user_id;

  const btnRow = compactActions ? 'gap-2 flex-wrap' : 'gap-4';

  return (
    <div className="flex gap-3 group">
      {profilePath ? (
        <Link to={profilePath} className="shrink-0">
          <img
            src={comment.author_avatar || `https://api.dicebear.com/7.x/avataaars/svg?seed=${comment.author_name}`}
            className="w-10 h-10 rounded-full bg-brand-800 ring-1 ring-white/10 hover:ring-brand-500/40 transition-all"
            alt=""
          />
        </Link>
      ) : (
        <img
          src={comment.author_avatar || `https://api.dicebear.com/7.x/avataaars/svg?seed=${comment.author_name}`}
          className="w-10 h-10 rounded-full bg-brand-800 shrink-0 ring-1 ring-white/10"
          alt=""
        />
      )}
      <div className="flex-1 min-w-0">
        <div className="flex items-center gap-2 mb-1 flex-wrap">
          {profilePath ? (
            <Link to={profilePath} className="font-bold text-sm text-white hover:text-brand-300 transition-colors">
              {comment.author_name || 'Аноним'}
            </Link>
          ) : (
            <span className="font-bold text-sm text-white">{comment.author_name || 'Аноним'}</span>
          )}
          <AuthorBadges comment={comment} />
          {showFriendRow && fs === 'none' ? (
            <button
              type="button"
              disabled={busy}
              onClick={() => onFriendAction(comment.user_id, 'request')}
              className="inline-flex items-center gap-1 text-[10px] font-bold uppercase tracking-wide px-2 py-0.5 rounded-full border border-brand-500/50 text-brand-300 hover:bg-brand-500/15 disabled:opacity-40"
            >
              <UserPlus className="w-3 h-3" />
              В друзья
            </button>
          ) : null}
          {showFriendRow && fs === 'outgoing' ? (
            <button
              type="button"
              disabled={busy}
              onClick={() => onFriendAction(comment.user_id, 'cancel')}
              className="inline-flex items-center gap-1 text-[10px] font-bold uppercase tracking-wide px-2 py-0.5 rounded-full border border-amber-500/40 text-amber-200/90 hover:bg-amber-500/10 disabled:opacity-40"
            >
              <X className="w-3 h-3" />
              Отменить заявку
            </button>
          ) : null}
          {showFriendRow && fs === 'incoming' ? (
            <>
              <button
                type="button"
                disabled={busy}
                onClick={() => onFriendAction(comment.user_id, 'accept')}
                className="inline-flex items-center gap-1 text-[10px] font-bold uppercase tracking-wide px-2 py-0.5 rounded-full border border-emerald-500/50 text-emerald-300 hover:bg-emerald-500/15 disabled:opacity-40"
              >
                <Check className="w-3 h-3" />
                Принять
              </button>
              <button
                type="button"
                disabled={busy}
                onClick={() => onFriendAction(comment.user_id, 'decline')}
                className="inline-flex items-center gap-1 text-[10px] font-bold uppercase tracking-wide px-2 py-0.5 rounded-full border border-brand-700/60 text-gray-400 hover:text-white disabled:opacity-40"
              >
                <X className="w-3 h-3" />
                Отклонить
              </button>
            </>
          ) : null}
          {showFriendRow && fs === 'friends' ? (
            <button
              type="button"
              disabled={busy}
              onClick={() => onFriendAction(comment.user_id, 'remove')}
              className="inline-flex items-center gap-1 text-[10px] font-bold uppercase tracking-wide px-2 py-0.5 rounded-full border border-brand-700/60 text-gray-400 hover:text-white hover:bg-brand-800/60 disabled:opacity-40"
            >
              <UserMinus className="w-3 h-3" />
              Убрать из друзей
            </button>
          ) : null}
          <span className="text-xs text-gray-500">{comment.created_at}</span>
        </div>
        <p className="text-sm text-gray-300 leading-relaxed break-words">{comment.body}</p>
        <div className={`flex items-center ${btnRow} mt-2`}>
          <button
            type="button"
            disabled={!isAuth}
            title={!isAuth ? 'Войдите, чтобы голосовать' : undefined}
            onClick={() => onVote(comment.id, nextVoteValue(my, 'up'))}
            className={`flex items-center gap-1.5 text-xs transition-colors ${
              my === 1 ? 'text-brand-400 font-semibold' : 'text-gray-500 hover:text-white'
            } disabled:opacity-40`}
          >
            <ThumbsUp className="w-3 h-3" /> {up}
          </button>
          <button
            type="button"
            disabled={!isAuth}
            title={!isAuth ? 'Войдите, чтобы голосовать' : undefined}
            onClick={() => onVote(comment.id, nextVoteValue(my, 'down'))}
            className={`flex items-center gap-1.5 text-xs transition-colors ${
              my === -1 ? 'text-amber-400 font-semibold' : 'text-gray-500 hover:text-white'
            } disabled:opacity-40`}
          >
            <ThumbsDown className="w-3 h-3" /> {down}
          </button>
          <button
            type="button"
            onClick={() => onReply(comment.id)}
            className="text-xs font-bold text-gray-500 hover:text-white transition-colors"
          >
            Ответить
          </button>
          {canDelete && (
            <button
              type="button"
              onClick={() => onDelete(comment.id)}
              className="text-xs text-gray-500 hover:text-red-400 transition-colors opacity-0 group-hover:opacity-100"
              title={canModerate && currentUserId !== comment.user_id ? 'Удалить (модерация)' : 'Удалить'}
            >
              <Trash2 className="w-3 h-3" />
            </button>
          )}
        </div>

        {comment.replies?.length > 0 && (
          <div className="mt-3">
            <button
              type="button"
              onClick={() => setShowReplies(!showReplies)}
              className="flex items-center gap-1 text-xs text-brand-400 hover:text-brand-300 font-medium"
            >
              <ChevronDown className={`w-3 h-3 transition-transform ${showReplies ? 'rotate-180' : ''}`} />
              {comment.replies.length} {comment.replies.length === 1 ? 'ответ' : 'ответов'}
            </button>
            {showReplies && (
              <div className="mt-3 space-y-4 pl-2 border-l-2 border-brand-700/50">
                {comment.replies.map((r) => (
                  <CommentItem
                    key={r.id}
                    comment={r}
                    onReply={onReply}
                    onDelete={onDelete}
                    onVote={onVote}
                    onFriendAction={onFriendAction}
                    currentUserId={currentUserId}
                    canModerate={canModerate}
                    isAuth={isAuth}
                    followBusyId={followBusyId}
                    compactActions={compactActions}
                  />
                ))}
              </div>
            )}
          </div>
        )}
      </div>
    </div>
  );
}

export default function CommentSection({
  clipId,
  variant = 'default',
  onCommentCountChange,
  onRequestClose,
}) {
  const [commentsList, setCommentsList] = useState([]);
  const [text, setText] = useState('');
  const [replyTo, setReplyTo] = useState(null);
  const [loading, setLoading] = useState(false);
  const [sort, setSort] = useState('recent');
  const [followBusyId, setFollowBusyId] = useState(null);
  const { isAuth, user } = useAuthStore();
  const canModerate = user?.role === 1 || user?.role === 2;
  const isFeedPanel = variant === 'feedPanel';

  const reloadComments = useCallback(() => {
    if (!clipId) return;
    commentsApi
      .list(clipId, { limit: 50, offset: 0, sort })
      .then((data) => setCommentsList(data.comments || []))
      .catch(() => {});
  }, [clipId, sort]);

  useEffect(() => {
    if (!clipId) return;
    setLoading(true);
    onCommentCountChange?.(0);
    commentsApi
      .list(clipId, { limit: 50, offset: 0, sort })
      .then((data) => setCommentsList(data.comments || []))
      .catch(() => {})
      .finally(() => setLoading(false));
  }, [clipId, sort, onCommentCountChange]);

  useEffect(() => {
    onCommentCountChange?.(commentsList.length);
  }, [commentsList.length, onCommentCountChange]);

  const handleVote = async (id, value) => {
    if (!isAuth) return;
    try {
      await commentsApi.vote(id, value);
      reloadComments();
    } catch {
      /* ignore */
    }
  };

  const findCommentRowByUser = (list, uid) => {
    for (const c of list) {
      if (c.user_id === uid) return c;
      if (c.replies?.length) {
        const inner = findCommentRowByUser(c.replies, uid);
        if (inner) return inner;
      }
    }
    return null;
  };

  const handleFriendComment = async (targetUserId, action) => {
    if (!isAuth || !targetUserId) return;
    setFollowBusyId(targetUserId);
    try {
      if (action === 'request') await socialApi.friendRequest(targetUserId);
      else if (action === 'cancel') await socialApi.cancelFriendRequest(targetUserId);
      else if (action === 'accept') await socialApi.acceptFriend(targetUserId);
      else if (action === 'decline') await socialApi.declineFriend(targetUserId);
      else if (action === 'remove') await socialApi.removeFriend(targetUserId);
      reloadComments();
    } catch {
      /* ignore */
    } finally {
      setFollowBusyId(null);
    }
  };

  const handleSubmit = async () => {
    if (!text.trim() || !isAuth) return;
    try {
      await commentsApi.create(clipId, text.trim(), replyTo || undefined);
      setText('');
      setReplyTo(null);
      reloadComments();
    } catch {
      /* ignore */
    }
  };

  const handleDelete = async (id) => {
    try {
      await commentsApi.remove(id);
      reloadComments();
    } catch {
      /* ignore */
    }
  };

  const inputBlock = isAuth ? (
    <div className={`flex gap-3 ${isFeedPanel ? 'mb-5' : 'gap-4 mb-8'}`}>
      <img
        src={user?.avatar_url || `https://api.dicebear.com/7.x/avataaars/svg?seed=${user?.username || 'User'}`}
        className={`rounded-full bg-brand-800 shrink-0 ${isFeedPanel ? 'w-9 h-9' : 'w-10 h-10'}`}
        alt=""
      />
      <div className="flex-1 min-w-0">
        {replyTo && (
          <div className="flex items-center gap-2 mb-2 text-xs text-brand-400">
            <Reply className="w-3 h-3" /> Ответ на комментарий
            <button type="button" onClick={() => setReplyTo(null)} className="text-gray-500 hover:text-white ml-2">
              ✕
            </button>
          </div>
        )}
        <input
          type="text"
          value={text}
          onChange={(e) => setText(e.target.value)}
          onKeyDown={(e) => e.key === 'Enter' && handleSubmit()}
          placeholder="Оставьте комментарий..."
          className={`w-full bg-transparent text-white focus:outline-none focus:border-brand-500 transition-colors placeholder-gray-500 ${
            isFeedPanel
              ? 'rounded-lg border border-white/10 bg-black/20 px-3 py-2.5 text-sm'
              : 'border-b border-brand-700 pb-2 placeholder-gray-600'
          }`}
        />
        <div className={`flex justify-end ${isFeedPanel ? 'mt-2' : 'mt-2'}`}>
          <button
            type="button"
            onClick={handleSubmit}
            disabled={!text.trim()}
            className="px-4 py-1.5 bg-brand-500 hover:bg-brand-600 text-white text-sm font-bold rounded-full transition-colors disabled:opacity-50 disabled:cursor-not-allowed"
          >
            Комментировать
          </button>
        </div>
      </div>
    </div>
  ) : (
    <p className={`text-sm text-gray-500 leading-relaxed ${isFeedPanel ? 'mb-4 px-4' : 'mb-6'}`}>
      Войдите через Twitch, чтобы комментировать и чтобы ваши действия шли в профиль и персональную ленту на
      ClipStream — не только просмотр «как на Twitch».
    </p>
  );

  const sortToggle = (
    <div className="flex rounded-lg border border-brand-700/60 overflow-hidden text-[11px] font-semibold shrink-0">
      <button
        type="button"
        onClick={() => setSort('recent')}
        className={`px-2.5 py-1.5 transition-colors ${sort === 'recent' ? 'bg-brand-600 text-white' : 'text-gray-400 hover:text-white bg-brand-950/40'}`}
      >
        Новые
      </button>
      <button
        type="button"
        onClick={() => setSort('best')}
        className={`px-2.5 py-1.5 inline-flex items-center gap-1 border-l border-brand-700/60 transition-colors ${
          sort === 'best' ? 'bg-brand-600 text-white' : 'text-gray-400 hover:text-white bg-brand-950/40'
        }`}
        title="По голосам сообщества"
      >
        <Sparkles className="w-3 h-3" />
        Лучшие
      </button>
    </div>
  );

  const listBlock = loading ? (
    <div className={`text-center text-gray-500 ${isFeedPanel ? 'py-10' : 'py-8'}`}>Загрузка...</div>
  ) : commentsList.length === 0 ? (
    <div className={`text-center text-gray-500 ${isFeedPanel ? 'py-10 text-sm' : 'py-8'}`}>
      Пока нет комментариев. Будьте первым!
    </div>
  ) : (
    <div className={`space-y-5 ${isFeedPanel ? '' : 'space-y-6'}`}>
      {commentsList.map((c) => (
        <CommentItem
          key={c.id}
          comment={c}
          onReply={setReplyTo}
          onDelete={handleDelete}
          onVote={handleVote}
          onFriendAction={handleFriendComment}
          currentUserId={user?.id}
          canModerate={canModerate}
          isAuth={isAuth}
          followBusyId={followBusyId}
          compactActions={isFeedPanel}
        />
      ))}
    </div>
  );

  if (isFeedPanel) {
    return (
      <div className="flex h-full min-h-0 flex-col rounded-xl border border-white/10 bg-white/[0.04] backdrop-blur-md shadow-inner shadow-black/25 overflow-hidden">
        <div className="flex items-center justify-between gap-2 px-4 pt-3 pb-2.5 border-b border-white/10 shrink-0 flex-wrap">
          <h3 className="text-sm font-bold text-white tracking-tight min-w-0">
            Комментарии{' '}
            <span className="text-gray-500 font-semibold">({commentsList.length})</span>
          </h3>
          <div className="flex items-center gap-2 shrink-0">
            {sortToggle}
            {onRequestClose ? (
              <button
                type="button"
                onClick={onRequestClose}
                className="p-2 rounded-lg text-gray-400 hover:text-white hover:bg-white/10 text-lg leading-none w-9 h-9 flex items-center justify-center"
                aria-label="Закрыть комментарии"
              >
                ×
              </button>
            ) : null}
          </div>
        </div>
        <div className="flex min-h-0 flex-1 flex-col px-4 py-3">
          <div className="shrink-0">{inputBlock}</div>
          <div className="mt-3 min-h-0 flex-1 overflow-y-auto overscroll-y-contain pr-0.5 [scrollbar-width:thin]">
            {listBlock}
          </div>
        </div>
      </div>
    );
  }

  return (
    <div className="glass-panel rounded-xl p-5 animate-fade-in" style={{ animationDelay: '0.1s' }}>
      <div className="flex justify-between items-center mb-6 flex-wrap gap-3">
        <h3 className="text-lg font-bold text-white flex items-center gap-2">
          <MessageSquare className="w-5 h-5 text-brand-500" />
          Комментарии
          <span className="text-gray-500 font-normal text-sm ml-1">({commentsList.length})</span>
        </h3>
        {sortToggle}
      </div>

      {inputBlock}
      {listBlock}
    </div>
  );
}
