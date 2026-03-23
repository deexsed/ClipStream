import { useState, useEffect, useRef, useCallback, useMemo } from 'react';
import { useNavigate } from 'react-router-dom';
import { Search } from 'lucide-react';
import { clips as clipsApi, channels as channelsApi, social as socialApi } from '../../api/client';

const DEBOUNCE_MS = 280;
const MIN_LEN = 2;

function formatDur(sec) {
  const s = Math.max(0, Number(sec) || 0);
  const m = Math.floor(s / 60);
  const r = Math.floor(s % 60);
  return `${m}:${String(r).padStart(2, '0')}`;
}

/**
 * Поиск в шапке: клипы; люди/каналы — одна секция (дубликат Twitch + профиль при том же логине не показываем).
 */
export default function HeaderSearch({
  value,
  onValueChange,
  className = '',
  variant = 'desktop',
  autoFocus = false,
  onNavigate,
}) {
  const navigate = useNavigate();
  const rootRef = useRef(null);
  const fetchGen = useRef(0);
  const debounceTimer = useRef(null);
  const [open, setOpen] = useState(false);
  const [loading, setLoading] = useState(false);
  const [clips, setClips] = useState([]);
  const [channels, setChannels] = useState([]);
  const [users, setUsers] = useState([]);

  const runSearch = useCallback((q) => {
    const t = q.trim();
    if (t.length < MIN_LEN) {
      setClips([]);
      setChannels([]);
      setUsers([]);
      setLoading(false);
      return;
    }
    const id = ++fetchGen.current;
    setLoading(true);
    Promise.all([
      clipsApi.search(t, 6).then((d) => d.clips || []).catch(() => []),
      channelsApi.search(t, 6).then((d) => d.channels || []).catch(() => []),
      socialApi.searchUsers(t, 8).then((d) => d.users || []).catch(() => []),
    ]).then(([c, ch, u]) => {
      if (id !== fetchGen.current) return;
      setClips(c);
      setChannels(ch);
      setUsers(u);
      setLoading(false);
    });
  }, []);

  useEffect(() => {
    if (debounceTimer.current) clearTimeout(debounceTimer.current);
    const t = value.trim();
    if (t.length < MIN_LEN) {
      setClips([]);
      setChannels([]);
      setUsers([]);
      setLoading(false);
      fetchGen.current += 1;
      return;
    }
    debounceTimer.current = setTimeout(() => runSearch(value), DEBOUNCE_MS);
    return () => clearTimeout(debounceTimer.current);
  }, [value, runSearch]);

  useEffect(() => {
    const onDoc = (e) => {
      if (rootRef.current && !rootRef.current.contains(e.target)) setOpen(false);
    };
    document.addEventListener('mousedown', onDoc);
    return () => document.removeEventListener('mousedown', onDoc);
  }, []);

  useEffect(() => {
    if (!open) return;
    const onKey = (e) => {
      if (e.key === 'Escape') setOpen(false);
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [open]);

  const finishNavigate = () => {
    setOpen(false);
    onNavigate?.();
  };

  const goClip = (id) => {
    navigate(`/clip/${id}`);
    finishNavigate();
  };

  const goChannel = (login) => {
    if (!login) return;
    navigate(`/channel/${encodeURIComponent(login)}`);
    finishNavigate();
  };

  const goUser = (username) => {
    navigate(`/u/${encodeURIComponent(username)}`);
    finishNavigate();
  };

  const submitAll = (e) => {
    e.preventDefault();
    const t = value.trim();
    if (!t) return;
    setOpen(false);
    navigate(`/?q=${encodeURIComponent(t)}`);
    finishNavigate();
  };

  const trimmed = value.trim();

  const channelByLoginLower = useMemo(() => {
    const m = new Map();
    (channels || []).forEach((ch) => {
      const k = String(ch.broadcaster_login || '').toLowerCase();
      if (k) m.set(k, ch);
    });
    return m;
  }, [channels]);

  const orphanChannels = useMemo(
    () =>
      (channels || []).filter((ch) => {
        const k = String(ch.broadcaster_login || '').toLowerCase();
        if (!k) return false;
        return !(users || []).some((u) => String(u.username || '').toLowerCase() === k);
      }),
    [channels, users],
  );

  const hasPeopleOrChannels = (users || []).length > 0 || orphanChannels.length > 0;
  const hasResults = clips.length > 0 || hasPeopleOrChannels;
  const showPanel = open && trimmed.length >= MIN_LEN;

  const inputClass =
    variant === 'desktop'
      ? 'w-full bg-brand-800/70 border border-brand-700 text-sm rounded-full py-2.5 px-4 pl-10 pr-3 focus:outline-none focus:border-brand-500 focus:ring-1 focus:ring-brand-500 transition-all text-gray-200 placeholder-gray-500'
      : 'w-full bg-brand-800/70 border border-brand-700 text-sm rounded-xl py-2.5 px-4 pl-10 pr-3 focus:outline-none focus:border-brand-500 focus:ring-1 focus:ring-brand-500 text-gray-200 placeholder-gray-500';

  return (
    <form ref={rootRef} onSubmit={submitAll} className={`relative ${className}`}>
      <div className="relative group">
        <input
          type="search"
          value={value}
          autoFocus={autoFocus}
          onChange={(e) => onValueChange(e.target.value)}
          onFocus={() => setOpen(true)}
          placeholder="Клипы, стримеры, каналы, профили…"
          className={inputClass}
          autoComplete="off"
          aria-expanded={showPanel}
          aria-controls="header-search-results"
        />
        <Search
          className="absolute left-3.5 top-1/2 -translate-y-1/2 w-4 h-4 text-gray-500 group-focus-within:text-brand-500 pointer-events-none"
          strokeWidth={2.25}
        />
      </div>

      {showPanel ? (
        <div
          id="header-search-results"
          className="absolute left-0 right-0 top-full mt-1.5 z-[60] rounded-xl border border-brand-600/80 bg-[#14141a] shadow-2xl shadow-black/60 max-h-[min(70vh,420px)] overflow-y-auto overscroll-contain"
          role="listbox"
        >
          {loading ? (
            <div className="px-3 py-4 text-center text-xs text-gray-500">Поиск…</div>
          ) : !hasResults ? (
            <div className="px-3 py-4 text-center text-xs text-gray-500">Ничего не найдено</div>
          ) : (
            <>
              {clips.length > 0 ? (
                <div className="py-2">
                  <p className="px-3 pb-1 text-[10px] font-bold uppercase tracking-wider text-gray-500">Клипы</p>
                  <ul className="space-y-0.5">
                    {clips.map((c) => (
                      <li key={`c-${c.id}`}>
                        <button
                          type="button"
                          className="w-full flex gap-2.5 px-3 py-2 text-left hover:bg-brand-800/80 transition-colors rounded-lg mx-1"
                          onMouseDown={(e) => e.preventDefault()}
                          onClick={() => goClip(c.id)}
                        >
                          <img
                            src={c.thumbnail_url}
                            alt=""
                            className="w-16 h-9 rounded object-cover bg-black shrink-0"
                          />
                          <div className="min-w-0 flex-1">
                            <p className="text-sm text-white line-clamp-2 leading-snug">{c.title}</p>
                            <p className="text-[11px] text-gray-500 mt-0.5 truncate">
                              {c.broadcaster_name}
                              {c.creator_login ? ` · создатель ${c.creator_login}` : ''}
                              <span className="text-gray-600"> · {formatDur(c.duration)}</span>
                            </p>
                          </div>
                        </button>
                      </li>
                    ))}
                  </ul>
                </div>
              ) : null}

              {hasPeopleOrChannels ? (
                <div className={`py-2 ${clips.length > 0 ? 'border-t border-brand-800/80' : ''}`}>
                  <p className="px-3 pb-1 text-[10px] font-bold uppercase tracking-wider text-gray-500">
                    Профили и каналы
                  </p>
                  <ul>
                    {(users || []).map((u) => {
                      const ch = channelByLoginLower.get(String(u.username || '').toLowerCase());
                      const title = ch?.display_name || u.username;
                      const avatarSrc =
                        u.avatar_url ||
                        ch?.thumbnail_url ||
                        `https://api.dicebear.com/7.x/avataaars/svg?seed=${encodeURIComponent(u.username || 'u')}`;
                      return (
                        <li key={`u-${u.id}`}>
                          <button
                            type="button"
                            className="w-full flex items-center gap-2.5 px-3 py-2 text-left hover:bg-brand-800/80 transition-colors rounded-lg mx-1"
                            onMouseDown={(e) => e.preventDefault()}
                            onClick={() => goUser(u.username)}
                          >
                            <img
                              src={avatarSrc}
                              alt=""
                              className="w-9 h-9 rounded-full object-cover shrink-0 border border-brand-700/50 bg-brand-900"
                            />
                            <div className="min-w-0 flex-1">
                              <p className="text-sm text-white truncate">{title}</p>
                              <p className="text-[11px] text-gray-500 truncate">
                                @{u.username}
                                {ch ? ' · Twitch + ClipStream' : ''}
                              </p>
                            </div>
                            {ch?.is_live ? (
                              <span className="text-[10px] font-bold text-red-400 shrink-0">LIVE</span>
                            ) : null}
                          </button>
                        </li>
                      );
                    })}
                    {orphanChannels.map((ch) => (
                      <li key={ch.id || ch.broadcaster_login}>
                        <button
                          type="button"
                          className="w-full flex items-center gap-2.5 px-3 py-2 text-left hover:bg-brand-800/80 transition-colors rounded-lg mx-1"
                          onMouseDown={(e) => e.preventDefault()}
                          onClick={() => goChannel(ch.broadcaster_login)}
                        >
                          <img
                            src={
                              ch.thumbnail_url ||
                              `https://api.dicebear.com/7.x/avataaars/svg?seed=${encodeURIComponent(ch.broadcaster_login || 'ch')}`
                            }
                            alt=""
                            className="w-9 h-9 rounded-full object-cover shrink-0 bg-brand-900"
                          />
                          <div className="min-w-0 flex-1">
                            <p className="text-sm text-white truncate">{ch.display_name || ch.broadcaster_login}</p>
                            <p className="text-[11px] text-gray-500 truncate">
                              @{ch.broadcaster_login}
                              <span className="text-gray-600"> · только Twitch</span>
                            </p>
                          </div>
                          {ch.is_live ? (
                            <span className="text-[10px] font-bold text-red-400 shrink-0">LIVE</span>
                          ) : null}
                        </button>
                      </li>
                    ))}
                  </ul>
                </div>
              ) : null}

              <div className="border-t border-brand-800/80 px-3 py-2 text-[11px] text-gray-600">
                Enter — полная выдача клипов на главной
              </div>
            </>
          )}
        </div>
      ) : null}
    </form>
  );
}
