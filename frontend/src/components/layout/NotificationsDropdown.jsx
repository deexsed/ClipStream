import { useCallback, useEffect, useRef, useState } from 'react';
import { Link, useNavigate } from 'react-router-dom';
import { Bell, CheckCheck, Loader2, MessageCircle, UserPlus, UserCheck, AtSign } from 'lucide-react';
import { notifications as notificationsApi } from '../../api/client';

function itemHref(n) {
  if (n.type === 'comment_reply' && n.payload?.clip_id) {
    return `/clip/${n.payload.clip_id}`;
  }
  if (n.type === 'new_follower' && n.actor_username) {
    return `/u/${encodeURIComponent(n.actor_username)}`;
  }
  if ((n.type === 'friend_request' || n.type === 'friend_accepted') && n.actor_username) {
    return `/u/${encodeURIComponent(n.actor_username)}`;
  }
  if (n.type === 'wall_mention' && n.payload?.profile_username) {
    return `/u/${encodeURIComponent(n.payload.profile_username)}?tab=wall`;
  }
  return null;
}

export default function NotificationsDropdown() {
  const [open, setOpen] = useState(false);
  const [items, setItems] = useState([]);
  const [unread, setUnread] = useState(0);
  const [loading, setLoading] = useState(false);
  const wrapRef = useRef(null);
  const navigate = useNavigate();

  const refreshUnread = useCallback(() => {
    notificationsApi
      .unreadCount()
      .then((d) => setUnread(typeof d.unread === 'number' ? d.unread : 0))
      .catch(() => {});
  }, []);

  const loadList = useCallback(() => {
    setLoading(true);
    notificationsApi
      .list('limit=40')
      .then((d) => setItems(Array.isArray(d.notifications) ? d.notifications : []))
      .catch(() => setItems([]))
      .finally(() => setLoading(false));
  }, []);

  useEffect(() => {
    refreshUnread();
    const t = window.setInterval(refreshUnread, 60000);
    return () => clearInterval(t);
  }, [refreshUnread]);

  useEffect(() => {
    if (!open) return;
    loadList();
  }, [open, loadList]);

  useEffect(() => {
    if (!open) return;
    const onDoc = (e) => {
      if (wrapRef.current && !wrapRef.current.contains(e.target)) setOpen(false);
    };
    document.addEventListener('mousedown', onDoc);
    return () => document.removeEventListener('mousedown', onDoc);
  }, [open]);

  const onMarkAll = async () => {
    try {
      await notificationsApi.markAllRead();
      refreshUnread();
      loadList();
    } catch {
      /* ignore */
    }
  };

  const onItemClick = async (n) => {
    const href = itemHref(n);
    if (!n.read && n.id) {
      try {
        await notificationsApi.markRead([n.id]);
      } catch {
        /* ignore */
      }
      refreshUnread();
    }
    setOpen(false);
    if (href) navigate(href);
  };

  return (
    <div className="relative" ref={wrapRef}>
      <button
        type="button"
        onClick={() => setOpen((v) => !v)}
        className="relative w-9 h-9 rounded-xl bg-brand-800/70 hover:bg-brand-700 flex items-center justify-center text-gray-300 hover:text-white transition-colors border border-brand-700/70"
        aria-expanded={open}
        aria-label="Уведомления"
        title="Уведомления"
      >
        <Bell className="w-4 h-4" />
        {unread > 0 ? (
          <span className="absolute -top-0.5 -right-0.5 min-w-[1.1rem] h-[1.1rem] px-0.5 rounded-full bg-fuchsia-500 text-[10px] font-bold text-white flex items-center justify-center">
            {unread > 99 ? '99+' : unread}
          </span>
        ) : null}
      </button>

      {open && (
        <div className="absolute right-0 top-full mt-2 w-[min(100vw-2rem,22rem)] rounded-xl border border-brand-700/60 bg-brand-900/98 backdrop-blur-xl shadow-2xl shadow-black/50 z-50 overflow-hidden">
          <div className="flex items-center justify-between gap-2 px-3 py-2.5 border-b border-brand-800/80">
            <span className="text-xs font-bold text-white">Уведомления</span>
            <button
              type="button"
              onClick={onMarkAll}
              className="inline-flex items-center gap-1 text-[11px] text-brand-400 hover:text-brand-300"
            >
              <CheckCheck className="w-3.5 h-3.5" />
              Прочитать всё
            </button>
          </div>
          <div className="max-h-[min(70vh,360px)] overflow-y-auto">
            {loading && items.length === 0 ? (
              <div className="flex justify-center py-10 text-gray-500">
                <Loader2 className="w-6 h-6 animate-spin" />
              </div>
            ) : null}
            {!loading && items.length === 0 ? (
              <p className="text-xs text-gray-500 text-center py-8 px-3">Пока пусто</p>
            ) : null}
            {items.map((n) => {
              const href = itemHref(n);
              const unreadRow = !n.read;
              const title =
                n.type === 'comment_reply'
                  ? `${n.actor_username || 'Кто-то'} ответил на ваш комментарий`
                  : n.type === 'new_follower'
                    ? `${n.actor_username || 'Кто-то'} подписался на вас`
                    : n.type === 'friend_request'
                      ? `${n.actor_username || 'Кто-то'} хочет добавиться в друзья`
                      : n.type === 'friend_accepted'
                        ? `${n.actor_username || 'Кто-то'} принял заявку в друзья`
                        : n.type === 'wall_mention'
                          ? `${n.actor_username || 'Кто-то'} упомянул вас в ленте профиля`
                          : n.type;
              const snippet = n.payload?.snippet;
              const rowIcon =
                n.type === 'comment_reply' ? (
                  <MessageCircle className="w-4 h-4" />
                ) : n.type === 'friend_accepted' ? (
                  <UserCheck className="w-4 h-4" />
                ) : n.type === 'wall_mention' ? (
                  <AtSign className="w-4 h-4" />
                ) : (
                  <UserPlus className="w-4 h-4" />
                );
              return (
                <div key={n.id} className="border-b border-brand-800/50 last:border-0">
                  {href ? (
                    <button
                      type="button"
                      onClick={() => onItemClick(n)}
                      className={`w-full text-left px-3 py-2.5 flex gap-2.5 transition-colors hover:bg-brand-800/60 ${
                        unreadRow ? 'bg-brand-800/25' : ''
                      }`}
                    >
                      <div className="shrink-0 mt-0.5 text-brand-400">{rowIcon}</div>
                      <div className="min-w-0 flex-1">
                        <p className="text-xs font-semibold text-white leading-snug">{title}</p>
                        {snippet ? (
                          <p className="text-[11px] text-gray-500 line-clamp-2 mt-0.5">&ldquo;{snippet}&rdquo;</p>
                        ) : null}
                        <p className="text-[10px] text-gray-600 mt-1">{n.created_at}</p>
                      </div>
                    </button>
                  ) : (
                    <div className="px-3 py-2.5 text-xs text-gray-500">{title}</div>
                  )}
                </div>
              );
            })}
          </div>
          <div className="px-3 py-2 border-t border-brand-800/80 text-center">
            <Link
              to="/"
              className="text-[11px] text-brand-400 hover:text-brand-300"
              onClick={() => setOpen(false)}
            >
              На главную
            </Link>
          </div>
        </div>
      )}
    </div>
  );
}
