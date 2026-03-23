import { useEffect, useLayoutEffect, useMemo, useCallback, useState } from 'react';
import { Link, useLocation } from 'react-router-dom';
import { LogIn, X } from 'lucide-react';
import { channels as channelsApi } from '../../api/client';
import useAuthStore from '../../store/useAuthStore';
import { useSubsSidebarStore } from '../../store/useSubsSidebarStore';
import SidebarLegalLinks from './SidebarLegalLinks';

function useSubscriptionsData() {
  const { isAuth } = useAuthStore();
  const [items, setItems] = useState([]);

  useEffect(() => {
    if (!isAuth) {
      setItems([]);
      return;
    }
    channelsApi
      .followed()
      .then((data) => setItems(data.channels || []))
      .catch(() => setItems([]));
  }, [isAuth]);

  const sorted = useMemo(() => {
    const arr = [...items];
    arr.sort((a, b) => {
      if (!!b.is_live !== !!a.is_live) return b.is_live ? 1 : -1;
      return (a.broadcaster_name || '').localeCompare(b.broadcaster_name || '');
    });
    return arr;
  }, [items]);

  return { isAuth, sorted };
}

function SubscriptionsList({ channels, isAuth, embedded }) {
  if (!isAuth) {
    return (
      <p className="text-xs text-gray-500 leading-relaxed">
        Войдите через Twitch — здесь появятся ваши подписки с Twitch, быстрый переход к клипам каналов и (на главной)
        блок «Друзья смотрят» по подпискам уже на ClipStream.
      </p>
    );
  }
  if (channels.length === 0) {
    return (
      <p className="text-xs text-gray-500 leading-relaxed">
        Подписки не подтянулись (нет доступа к follows или список пуст). Лента на главной и рекомендации всё равно
        работают от просмотров на сайте.
      </p>
    );
  }
  const inner = (
    <>
      {channels.map((ch) => (
        <Link
          key={ch.broadcaster_id || ch.broadcaster_login}
          to={`/channel/${encodeURIComponent(ch.broadcaster_login || ch.broadcaster_name || '')}`}
          className="flex items-start gap-2.5 px-2.5 py-2 rounded-xl border border-brand-800/60 hover:border-brand-600/60 bg-brand-900/45 hover:bg-brand-800/75 transition-colors"
          title={`Клипы канала ${ch.broadcaster_name}`}
        >
          <img
            src={
              ch.avatar_url ||
              `https://api.dicebear.com/7.x/avataaars/svg?seed=${ch.broadcaster_login || ch.broadcaster_name || 'user'}`
            }
            alt=""
            className="w-8 h-8 rounded-full border border-brand-700/60 bg-brand-800 shrink-0"
          />
          <div className="min-w-0 flex-1">
            <div className="flex items-center justify-between gap-2">
              <span className="text-sm text-gray-200 truncate">{ch.broadcaster_name || ch.broadcaster_login}</span>
              {ch.is_live ? (
                <span className="text-[10px] font-bold px-1.5 py-0.5 rounded bg-red-600 text-white uppercase">Live</span>
              ) : (
                <span className="w-2 h-2 rounded-full bg-gray-600 shrink-0" />
              )}
            </div>
            {ch.is_live ? (
              <>
                {ch.game_name && <p className="text-[11px] text-brand-400 truncate">{ch.game_name}</p>}
                {ch.stream_title && <p className="text-[11px] text-gray-400 truncate">{ch.stream_title}</p>}
              </>
            ) : (
              <p className="text-[11px] text-gray-500 truncate">Оффлайн</p>
            )}
          </div>
        </Link>
      ))}
    </>
  );
  if (embedded) {
    return (
      <div className="flex flex-col flex-1 min-h-0 w-full min-w-0">
        <div className="space-y-1.5 pr-1 flex-1 min-h-0 overflow-y-auto [scrollbar-width:thin] [scrollbar-color:rgba(113,113,122,0.45)_transparent]">
          {inner}
        </div>
      </div>
    );
  }
  return (
    <div className="space-y-1.5 pr-1 max-h-[calc(100vh-130px)] overflow-y-auto">{inner}</div>
  );
}

/** Свернутый режим: только аватары, вертикальный скролл (как mini-guide YouTube). */
function SubscriptionsAvatarRail({ channels, isAuth }) {
  if (!isAuth) {
    return (
      <div className="flex flex-col items-center justify-center py-2 px-1">
        <Link
          to="/"
          className="shrink-0 flex h-11 w-11 items-center justify-center rounded-full border border-brand-600/70 bg-brand-800/80 text-brand-400 hover:bg-brand-800 hover:text-brand-300 transition-colors"
          title="Войдите через Twitch — кнопка в шапке"
          aria-label="Войдите через Twitch (кнопка в шапке сайта)"
        >
          <LogIn className="w-5 h-5" strokeWidth={2.25} />
        </Link>
      </div>
    );
  }
  if (channels.length === 0) {
    return <p className="text-[10px] text-center text-gray-500 px-1 leading-tight">Нет каналов</p>;
  }

  return (
    <div className="flex flex-col items-center gap-2 w-full min-h-0 flex-1 overflow-y-auto overflow-x-hidden py-1 px-1.5 [scrollbar-width:thin] [scrollbar-color:rgba(113,113,122,0.5)_transparent]">
      {channels.map((ch) => {
        const login = ch.broadcaster_login || ch.broadcaster_name || '';
        const name = ch.broadcaster_name || ch.broadcaster_login || '';
        return (
          <Link
            key={ch.broadcaster_id || login}
            to={`/channel/${encodeURIComponent(login)}`}
            className={`shrink-0 rounded-full transition-transform hover:scale-105 focus:outline-none focus:ring-2 focus:ring-brand-500 focus:ring-offset-2 focus:ring-offset-brand-900 ${
              ch.is_live ? 'ring-2 ring-red-500 ring-offset-2 ring-offset-[#0a0c12]' : ''
            }`}
            title={name}
          >
            <img
              src={
                ch.avatar_url ||
                `https://api.dicebear.com/7.x/avataaars/svg?seed=${login || 'user'}`
              }
              alt=""
              className="w-10 h-10 rounded-full bg-brand-800 object-cover border border-brand-700/60"
            />
          </Link>
        );
      })}
    </div>
  );
}

export function SubscriptionsClipOverlayPortal() {
  const clipOverlayOpen = useSubsSidebarStore((s) => s.clipOverlayOpen);
  const setClipOverlayOpen = useSubsSidebarStore((s) => s.setClipOverlayOpen);
  const { isAuth, sorted } = useSubscriptionsData();

  const close = useCallback(() => setClipOverlayOpen(false), [setClipOverlayOpen]);

  useEffect(() => () => setClipOverlayOpen(false), [setClipOverlayOpen]);

  useEffect(() => {
    if (!clipOverlayOpen) return;
    const onKey = (e) => {
      if (e.key === 'Escape') close();
    };
    window.addEventListener('keydown', onKey);
    const prev = document.body.style.overflow;
    document.body.style.overflow = 'hidden';
    return () => {
      window.removeEventListener('keydown', onKey);
      document.body.style.overflow = prev;
    };
  }, [clipOverlayOpen, close]);

  if (!clipOverlayOpen) return null;

  return (
    <div
      className="fixed inset-0 z-40 flex justify-start bg-black/55 backdrop-blur-[2px] animate-fade-in"
      role="dialog"
      aria-modal="true"
      aria-label="Подписки Twitch"
      onClick={close}
    >
      <aside
        className="h-full w-full max-w-sm border-r border-brand-700/70 bg-brand-900/98 backdrop-blur-xl shadow-2xl flex flex-col animate-slide-in-left"
        onClick={(e) => e.stopPropagation()}
      >
        <div className="flex items-center justify-between gap-3 px-4 py-3 border-b border-brand-700/50 shrink-0">
          <h3 className="text-sm font-bold text-gray-200 uppercase tracking-wider">Подписки</h3>
          <button
            type="button"
            onClick={close}
            className="p-2 rounded-lg text-gray-400 hover:text-white hover:bg-brand-800/80 transition-colors"
            aria-label="Закрыть"
          >
            <X className="w-5 h-5" />
          </button>
        </div>
        <div className="flex-1 min-h-0 flex flex-col p-4 pt-3 gap-0 overflow-hidden min-w-0">
          <SubscriptionsList channels={sorted} isAuth={isAuth} embedded />
          <SidebarLegalLinks variant="expanded" />
        </div>
      </aside>
    </div>
  );
}

function SubscriptionsDock() {
  const dockCollapsed = useSubsSidebarStore((s) => s.dockCollapsed);
  const setDockCollapsed = useSubsSidebarStore((s) => s.setDockCollapsed);
  const { isAuth, sorted } = useSubscriptionsData();

  /** При входе как гость или после выхода — разворачиваем (из localStorage мог остаться узкий режим). Не трогаем dockCollapsed в deps — иначе гость не сможет свернуть вручную. */
  useLayoutEffect(() => {
    if (!isAuth) setDockCollapsed(false);
  }, [isAuth, setDockCollapsed]);

  return (
    <aside
      className={`hidden xl:flex shrink-0 flex-col h-full min-h-0 border-r border-brand-700/60 bg-brand-900/55 backdrop-blur-xl transition-[width] duration-200 ease-out overflow-hidden ${
        dockCollapsed ? 'w-[72px]' : 'w-80'
      }`}
    >
      {dockCollapsed ? (
        <div className="flex flex-col flex-1 min-h-0 w-full pt-2 pb-1">
          <div className="flex-1 min-h-0 min-w-0 overflow-hidden flex flex-col">
            <SubscriptionsAvatarRail channels={sorted} isAuth={isAuth} />
          </div>
          <SidebarLegalLinks variant="collapsed" />
        </div>
      ) : (
        <div className="w-80 min-w-[20rem] flex flex-col h-full min-h-0">
          <div className="px-4 pt-4 pb-2 shrink-0">
            <h3 className="text-xs font-bold text-gray-300 uppercase tracking-wider">Подписки</h3>
          </div>
          <div className="flex-1 min-h-0 px-4 pb-3 flex flex-col gap-0 overflow-hidden">
            <SubscriptionsList channels={sorted} isAuth={isAuth} embedded />
            <SidebarLegalLinks variant="expanded" />
          </div>
        </div>
      )}
    </aside>
  );
}

export default function FollowingSidebar() {
  const location = useLocation();
  const hideDock = location.pathname.startsWith('/clip/') || location.pathname.startsWith('/feed');

  if (hideDock) {
    return null;
  }
  return <SubscriptionsDock />;
}
