import { useState, useEffect } from 'react';
import { MessageSquare, ThumbsUp, ThumbsDown, Reply, Trash2, ChevronDown } from 'lucide-react';
import { comments as commentsApi } from '../../api/client';
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

function CommentItem({ comment, onReply, onDelete, currentUserId, canModerate }) {
  const [showReplies, setShowReplies] = useState(false);
  const canDelete = currentUserId === comment.user_id || canModerate;

  return (
    <div className="flex gap-3 group">
      <img
        src={comment.author_avatar || `https://api.dicebear.com/7.x/avataaars/svg?seed=${comment.author_name}`}
        className="w-10 h-10 rounded-full bg-brand-800 shrink-0"
        alt=""
      />
      <div className="flex-1 min-w-0">
        <div className="flex items-center gap-2 mb-1 flex-wrap">
          <span className="font-bold text-sm text-white">{comment.author_name || 'Аноним'}</span>
          <AuthorBadges comment={comment} />
          <span className="text-xs text-gray-500">{comment.created_at}</span>
        </div>
        <p className="text-sm text-gray-300 leading-relaxed break-words">{comment.body}</p>
        <div className="flex items-center gap-4 mt-2">
          <button className="flex items-center gap-1.5 text-xs text-gray-500 hover:text-white transition-colors">
            <ThumbsUp className="w-3 h-3" /> 0
          </button>
          <button className="flex items-center gap-1.5 text-xs text-gray-500 hover:text-white transition-colors">
            <ThumbsDown className="w-3 h-3" />
          </button>
          <button
            onClick={() => onReply(comment.id)}
            className="text-xs font-bold text-gray-500 hover:text-white transition-colors"
          >
            Ответить
          </button>
          {canDelete && (
            <button
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
                    currentUserId={currentUserId}
                    canModerate={canModerate}
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

export default function CommentSection({ clipId }) {
  const [commentsList, setCommentsList] = useState([]);
  const [text, setText] = useState('');
  const [replyTo, setReplyTo] = useState(null);
  const [loading, setLoading] = useState(false);
  const { isAuth, user } = useAuthStore();
  const canModerate = user?.role === 1 || user?.role === 2;

  useEffect(() => {
    if (!clipId) return;
    setLoading(true);
    commentsApi.list(clipId, 50, 0)
      .then((data) => setCommentsList(data.comments || []))
      .catch(() => {})
      .finally(() => setLoading(false));
  }, [clipId]);

  const reloadComments = () => {
    commentsApi
      .list(clipId, 50, 0)
      .then((data) => setCommentsList(data.comments || []))
      .catch(() => {});
  };

  const handleSubmit = async () => {
    if (!text.trim() || !isAuth) return;
    try {
      await commentsApi.create(clipId, text.trim(), replyTo || undefined);
      setText('');
      setReplyTo(null);
      reloadComments();
    } catch {}
  };

  const handleDelete = async (id) => {
    try {
      await commentsApi.remove(id);
      reloadComments();
    } catch {}
  };

  return (
    <div className="glass-panel rounded-xl p-5 animate-fade-in" style={{ animationDelay: '0.1s' }}>
      <div className="flex justify-between items-center mb-6">
        <h3 className="text-lg font-bold text-white flex items-center gap-2">
          <MessageSquare className="w-5 h-5 text-brand-500" />
          Комментарии
          <span className="text-gray-500 font-normal text-sm ml-1">({commentsList.length})</span>
        </h3>
      </div>

      {isAuth ? (
        <div className="flex gap-4 mb-8">
          <img
            src={user?.avatar_url || `https://api.dicebear.com/7.x/avataaars/svg?seed=${user?.username || 'User'}`}
            className="w-10 h-10 rounded-full bg-brand-800"
            alt=""
          />
          <div className="flex-1">
            {replyTo && (
              <div className="flex items-center gap-2 mb-2 text-xs text-brand-400">
                <Reply className="w-3 h-3" /> Ответ на комментарий
                <button onClick={() => setReplyTo(null)} className="text-gray-500 hover:text-white ml-2">✕</button>
              </div>
            )}
            <input
              type="text"
              value={text}
              onChange={(e) => setText(e.target.value)}
              onKeyDown={(e) => e.key === 'Enter' && handleSubmit()}
              placeholder="Оставьте комментарий..."
              className="w-full bg-transparent border-b border-brand-700 text-white pb-2 focus:outline-none focus:border-brand-500 transition-colors placeholder-gray-600"
            />
            <div className="flex justify-end mt-2">
              <button
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
        <p className="text-sm text-gray-500 mb-6">Войдите, чтобы оставить комментарий</p>
      )}

      {loading ? (
        <div className="text-center text-gray-500 py-8">Загрузка...</div>
      ) : commentsList.length === 0 ? (
        <div className="text-center text-gray-500 py-8">Пока нет комментариев. Будьте первым!</div>
      ) : (
        <div className="space-y-6">
          {commentsList.map((c) => (
            <CommentItem
              key={c.id}
              comment={c}
              onReply={setReplyTo}
              onDelete={handleDelete}
              currentUserId={user?.id}
              canModerate={canModerate}
            />
          ))}
        </div>
      )}
    </div>
  );
}
