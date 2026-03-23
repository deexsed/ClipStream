import { useCallback, useEffect, useRef, useState } from 'react';
import { useNavigate, useSearchParams, Link } from 'react-router-dom';
import {
  AlertTriangle,
  Flame,
  Clock,
  Timer,
  Search,
  Users,
  Home as HomeIcon,
  Rows3,
  Compass,
  RefreshCw,
  MessageCircle,
  Sparkles,
  Dices,
  ChevronRight,
  ChevronDown,
  CalendarRange,
  ListOrdered,
} from 'lucide-react';
import { clips as clipsApi, channels as channelsApi, social as socialApi } from '../api/client';
import ClipCard from '../components/clips/ClipCard';
import useLiveStatus from '../hooks/useLiveStatus';
import useInfiniteScroll from '../hooks/useInfiniteScroll';
import useAuthStore from '../store/useAuthStore';
import { useClipLangStore } from '../store/useClipLangStore';

const SORTS = [
  { key: 'popular', label: 'Популярные', icon: Flame },
  { key: 'recent', label: 'Новые', icon: Clock },
  { key: 'duration', label: 'Длинные', icon: Timer },
];
const PERIODS = [
  { key: 'today', label: 'Сегодня' },
  { key: 'week', label: 'Неделя' },
  { key: 'month', label: 'Месяц' },
  { key: 'all', label: 'Все время' },
];

const LS_SEARCH_PANEL_CHANNELS = 'clipstream.search.panel.channelsOpen';
const LS_SEARCH_PANEL_CLIPS = 'clipstream.search.panel.clipsOpen';

function loadSearchPanelOpen(storageKey, defaultOpen = true) {
  try {
    const v = localStorage.getItem(storageKey);
    if (v === null) return defaultOpen;
    return v === '1';
  } catch {
    return defaultOpen;
  }
}

function saveSearchPanelOpen(storageKey, open) {
  try {
    localStorage.setItem(storageKey, open ? '1' : '0');
  } catch {
    /* ignore */
  }
}

export default function Home() {
  const navigate = useNavigate();
  const [searchParams, setSearchParams] = useSearchParams();
  const [searchClips, setSearchClips] = useState([]);
  /** Смешанная лента главной: shelf = for_you | trending | fresh */
  const [feedRows, setFeedRows] = useState([]);
  const [feedLoading, setFeedLoading] = useState(false);
  const [feedHasMore, setFeedHasMore] = useState(true);
  const [sort, setSort] = useState('popular');
  const [period, setPeriod] = useState('all');
  const [searchOffset, setSearchOffset] = useState(0);
  const [searchHasMore, setSearchHasMore] = useState(true);
  const [loading, setLoading] = useState(false);
  const [initialLoading, setInitialLoading] = useState(true);
  const [oauthError, setOauthError] = useState('');
  const [similarChannels, setSimilarChannels] = useState([]);
  const [friendsClips, setFriendsClips] = useState([]);
  const [homeFeedError, setHomeFeedError] = useState(false);
  const [searchFeedError, setSearchFeedError] = useState(false);
  const [searchAppendError, setSearchAppendError] = useState(false);
  const [feedLoadMoreError, setFeedLoadMoreError] = useState(false);
  const [discussedClips, setDiscussedClips] = useState([]);
  const [channelUpdateClips, setChannelUpdateClips] = useState([]);
  const [searchChannels, setSearchChannels] = useState([]);
  const [searchUsers, setSearchUsers] = useState([]);
  const [searchMetaLoading, setSearchMetaLoading] = useState(false);
  const [channelsPanelOpen, setChannelsPanelOpen] = useState(() =>
    loadSearchPanelOpen(LS_SEARCH_PANEL_CHANNELS, true),
  );
  const [clipsPanelOpen, setClipsPanelOpen] = useState(() => loadSearchPanelOpen(LS_SEARCH_PANEL_CLIPS, true));
  const q = (searchParams.get('q') || '').trim();
  const { isAuth } = useAuthStore();
  const clipLangKey = useClipLangStore((s) => s.codes.join(','));
  const homeSessionRef = useRef(null);
  const feedFetchGenRef = useRef(0);
  const seenClipIdsRef = useRef(new Set());
  if (homeSessionRef.current === null) {
    homeSessionRef.current =
      typeof crypto !== 'undefined' && typeof crypto.randomUUID === 'function'
        ? crypto.randomUUID()
        : `h_${Date.now()}_${Math.random().toString(36).slice(2, 12)}`;
  }

  const liveSource = q
    ? [
        ...searchClips,
        ...searchChannels.map((ch) => ({
          broadcaster_name: ch.broadcaster_login || ch.display_name || '',
        })),
      ]
    : [
        ...friendsClips,
        ...channelUpdateClips,
        ...discussedClips,
        ...feedRows.map((r) => r.clip),
      ];
  const liveMap = useLiveStatus(liveSource);

  useEffect(() => {
    const error = searchParams.get('error');
    const desc  = searchParams.get('error_description');
    if (error) {
      if (error === 'redirect_mismatch') {
        setOauthError(
          'Вход через Twitch для этого сайта настроен неверно. Обратитесь к администратору или проверьте адрес возврата после входа в настройках приложения Twitch.',
        );
      } else {
        setOauthError(desc ? `Не удалось войти через Twitch: ${desc}` : 'Не удалось войти через Twitch. Попробуйте ещё раз.');
      }
      setSearchParams({}, { replace: true });
    }
  }, []);

  const fetchSearchPage = useCallback((nextOffset, replace = false) => {
    if (replace) setInitialLoading(true);
    setLoading(true);
    setSearchFeedError(false);
    setSearchAppendError(false);
    const params = new URLSearchParams({
      sort,
      period,
      limit: '20',
      offset: String(nextOffset),
    });
    if (q) params.set('search', q);
    clipsApi
      .feed(params.toString())
      .then((data) => {
        const incoming = data.clips || [];
        setSearchClips((prev) => (replace ? incoming : [...prev, ...incoming]));
        setSearchOffset(nextOffset + incoming.length);
        setSearchHasMore(incoming.length >= 20);
        setSearchAppendError(false);
      })
      .catch(() => {
        if (replace) {
          setSearchFeedError(true);
          setSearchClips([]);
          setSearchOffset(0);
          setSearchHasMore(false);
        } else {
          setSearchAppendError(true);
          setSearchHasMore(false);
        }
      })
      .finally(() => setLoading(false));
  }, [sort, period, q, clipLangKey]);

  /** Поиск — отдельный эффект. Не смешивать с главной: иначе при смене fetchSearchPage лента главной перезапускалась и могла затереться гонкой. */
  useEffect(() => {
    if (!q) {
      setSearchFeedError(false);
      setSearchAppendError(false);
      return;
    }
    setSearchClips([]);
    setSearchOffset(0);
    setSearchHasMore(true);
    fetchSearchPage(0, true);
  }, [q, fetchSearchPage]);

  /** Каналы Twitch и профили ClipStream для страницы поиска (как в шапке, но полноэкранно). */
  useEffect(() => {
    if (!q) {
      setSearchChannels([]);
      setSearchUsers([]);
      setSearchMetaLoading(false);
      return;
    }
    let cancelled = false;
    setSearchMetaLoading(true);
    setSearchChannels([]);
    setSearchUsers([]);
    const promises = [channelsApi.search(q, 16).catch(() => ({ channels: [] }))];
    if (q.length >= 2) {
      promises.push(socialApi.searchUsers(q, 20).catch(() => ({ users: [] })));
    }
    Promise.all(promises).then((results) => {
      if (cancelled) return;
      setSearchChannels(results[0].channels || []);
      setSearchUsers(q.length >= 2 ? results[1]?.users || [] : []);
      setSearchMetaLoading(false);
    });
    return () => {
      cancelled = true;
    };
  }, [q]);

  /** Главная: смешанная лента `/clips/home-feed` (быстрее старой тройки запросов + без дублей при подгрузке). */
  useEffect(() => {
    if (q) return;
    const gen = ++feedFetchGenRef.current;
    setHomeFeedError(false);
    setFeedLoadMoreError(false);
    setLoading(true);
    setInitialLoading(true);
    setFeedHasMore(true);
    seenClipIdsRef.current = new Set();
    setFeedRows([]);

    clipsApi
      .homeFeed(24, homeSessionRef.current, '')
      .then((data) => {
        if (gen !== feedFetchGenRef.current) return;
        const items = data.items || [];
        const rows = [];
        for (const row of items) {
          const c = row.clip;
          if (!c || !c.id) continue;
          if (seenClipIdsRef.current.has(c.id)) continue;
          seenClipIdsRef.current.add(c.id);
          rows.push({ shelf: row.shelf || 'for_you', clip: c });
        }
        setFeedRows(rows);
        setFeedHasMore(items.length >= 18);
      })
      .catch(() => {
        if (gen !== feedFetchGenRef.current) return;
        setHomeFeedError(true);
        setFeedRows([]);
        setFeedHasMore(false);
      })
      .finally(() => {
        if (gen !== feedFetchGenRef.current) return;
        setLoading(false);
      });
  }, [q, isAuth, clipLangKey]);

  useEffect(() => {
    if (q || !isAuth) {
      setSimilarChannels([]);
      return;
    }
    channelsApi.similar(8)
      .then((data) => setSimilarChannels(data.channels || []))
      .catch(() => setSimilarChannels([]));
  }, [q, isAuth]);

  useEffect(() => {
    if (q || !isAuth) {
      setFriendsClips([]);
      return;
    }
    socialApi
      .friendsFeed('limit=20&offset=0')
      .then((data) => setFriendsClips(data.clips || []))
      .catch(() => setFriendsClips([]));
  }, [q, isAuth, clipLangKey]);

  useEffect(() => {
    if (q) {
      setDiscussedClips([]);
      return;
    }
    clipsApi
      .discussed('hours=24&limit=12')
      .then((data) => setDiscussedClips(data.clips || []))
      .catch(() => setDiscussedClips([]));
  }, [q, clipLangKey]);

  useEffect(() => {
    if (q || !isAuth) {
      setChannelUpdateClips([]);
      return;
    }
    clipsApi
      .channelUpdates('limit=12')
      .then((data) => setChannelUpdateClips(data.clips || []))
      .catch(() => setChannelUpdateClips([]));
  }, [q, isAuth, clipLangKey]);

  useEffect(() => {
    if (!loading) setInitialLoading(false);
  }, [loading]);

  const loadMore = useCallback(() => {
    if (!q || loading || !searchHasMore) return;
    fetchSearchPage(searchOffset, false);
  }, [q, loading, searchHasMore, fetchSearchPage, searchOffset]);

  const retrySearchAppend = useCallback(() => {
    setSearchAppendError(false);
    setSearchHasMore(true);
    fetchSearchPage(searchOffset, false);
  }, [fetchSearchPage, searchOffset]);
  const lastRef = useInfiniteScroll(loadMore, searchHasMore, loading);

  const loadMoreFeed = useCallback((opts) => {
    const force = opts && opts.force;
    if (q || feedLoading || (!feedHasMore && !force)) return;
    if (force) setFeedLoadMoreError(false);
    setFeedLoading(true);
    const ids = Array.from(seenClipIdsRef.current);
    const exclude = ids.length > 180 ? ids.slice(-180).join(',') : ids.join(',');
    clipsApi
      .homeFeed(20, homeSessionRef.current, exclude)
      .then((data) => {
        const items = data.items || [];
        if (items.length === 0) {
          setFeedHasMore(false);
          return;
        }
        setFeedRows((prev) => {
          const seen = new Set(prev.map((r) => r.clip.id));
          const next = [...prev];
          for (const row of items) {
            const c = row.clip;
            if (!c || !c.id || seen.has(c.id)) continue;
            seen.add(c.id);
            seenClipIdsRef.current.add(c.id);
            next.push({ shelf: row.shelf || 'for_you', clip: c });
          }
          return next;
        });
        setFeedHasMore(items.length >= 15);
      })
      .catch(() => {
        setFeedLoadMoreError(true);
        setFeedHasMore(false);
      })
      .finally(() => setFeedLoading(false));
  }, [q, feedLoading, feedHasMore, clipLangKey]);

  const retryLoadMoreFeed = useCallback(() => {
    setFeedHasMore(true);
    loadMoreFeed({ force: true });
  }, [loadMoreFeed]);

  const lastFeedRef = useInfiniteScroll(loadMoreFeed, feedHasMore, feedLoading);

  const goClip = (clip) => navigate(`/clip/${clip.id}`);

  const goRandomClip = useCallback(() => {
    clipsApi
      .random()
      .then((data) => {
        const id = data.clip?.id;
        if (id) navigate(`/clip/${id}`);
      })
      .catch(() => {});
  }, [navigate]);

  const toggleChannelsPanel = useCallback(() => {
    setChannelsPanelOpen((prev) => {
      const next = !prev;
      saveSearchPanelOpen(LS_SEARCH_PANEL_CHANNELS, next);
      return next;
    });
  }, []);

  const toggleClipsPanel = useCallback(() => {
    setClipsPanelOpen((prev) => {
      const next = !prev;
      saveSearchPanelOpen(LS_SEARCH_PANEL_CLIPS, next);
      return next;
    });
  }, []);

  const filterChipClass = (active) =>
    `px-3 py-2 rounded-lg text-sm border transition-colors ${
      active
        ? 'bg-brand-500 border-brand-500 text-white shadow-sm shadow-brand-500/20'
        : 'bg-brand-800/70 border-brand-700 text-gray-300 hover:text-white hover:border-brand-600'
    }`;

  const showHomeSkeleton = !q && (loading || initialLoading) && feedRows.length === 0;

  const showSearchChannelsBlock = Boolean(q && (searchMetaLoading || searchChannels.length > 0));
  const showSearchUsersBlock = Boolean(q && q.length >= 2 && (searchMetaLoading || searchUsers.length > 0));
  const searchTotallyEmpty = Boolean(
    q &&
      !searchMetaLoading &&
      !loading &&
      !initialLoading &&
      !searchFeedError &&
      searchClips.length === 0 &&
      searchChannels.length === 0 &&
      searchUsers.length === 0,
  );

  return (
    <div className="p-4 lg:p-6 max-w-[1760px] mx-auto animate-fade-in">
      {homeFeedError && !q && (
        <div className="mb-6 p-4 bg-amber-950/40 border border-amber-600/40 rounded-xl flex items-start gap-3 text-amber-100/90 text-sm">
          <AlertTriangle className="w-5 h-5 text-amber-400 shrink-0 mt-0.5" />
          <p>Не удалось загрузить ленту. Проверьте соединение и обновите страницу.</p>
        </div>
      )}

      {oauthError && (
        <div className="mb-6 p-4 bg-red-900/30 border border-red-700/50 rounded-xl flex items-start gap-3 animate-fade-in">
          <AlertTriangle className="w-5 h-5 text-red-400 shrink-0 mt-0.5" />
          <div className="flex-1">
            <p className="text-red-300 text-sm font-medium">Ошибка авторизации</p>
            <p className="text-red-400/80 text-xs mt-1">{oauthError}</p>
          </div>
          <button onClick={() => setOauthError('')} className="text-red-500 text-lg leading-none hover:text-red-300">&times;</button>
        </div>
      )}

      {q && (
        <header className="mb-5 pb-4 border-b border-brand-800/70">
          <div className="flex flex-wrap items-start justify-between gap-3 gap-y-2">
            <div className="min-w-0">
              <h1 className="text-lg sm:text-xl font-bold text-white tracking-tight">
                Результаты для <span className="text-brand-400">«{q}»</span>
              </h1>
              <p className="text-xs text-gray-500 mt-1">Каналы, профили и клипы — листайте страницу вниз</p>
            </div>
            <button
              type="button"
              onClick={() =>
                setSearchParams((prev) => {
                  const next = new URLSearchParams(prev);
                  next.delete('q');
                  return next;
                })
              }
              className="shrink-0 text-xs text-gray-400 hover:text-white border border-brand-700 rounded-full px-3 py-1.5 hover:bg-brand-800/80 transition-colors"
            >
              Очистить запрос
            </button>
          </div>
        </header>
      )}

      {showSearchChannelsBlock && (
        <section
          className="mb-7 rounded-xl border border-brand-700/55 bg-brand-900/35 overflow-hidden"
          aria-labelledby="search-channels-heading"
        >
          <button
            type="button"
            id="search-channels-heading"
            onClick={toggleChannelsPanel}
            aria-expanded={channelsPanelOpen}
            aria-controls="search-channels-panel"
            className="w-full flex items-center justify-between gap-3 px-4 py-3.5 text-left hover:bg-brand-800/35 transition-colors"
          >
            <div className="min-w-0">
              <span className="text-sm font-semibold text-white uppercase tracking-wide">Каналы Twitch</span>
              <p className="text-[11px] text-gray-500 mt-0.5">
                {!searchMetaLoading ? (
                  <>
                    Найдено: <span className="text-gray-400 tabular-nums">{searchChannels.length}</span>
                    {!channelsPanelOpen ? ' · свернуто' : ''}
                  </>
                ) : (
                  'Загрузка…'
                )}
              </p>
            </div>
            <ChevronDown
              className={`w-5 h-5 text-gray-500 shrink-0 transition-transform duration-200 ${
                channelsPanelOpen ? 'rotate-180' : ''
              }`}
              strokeWidth={2}
            />
          </button>
          {channelsPanelOpen ? (
            <div
              id="search-channels-panel"
              className="border-t border-brand-800/70 divide-y divide-brand-800/70"
            >
              {searchMetaLoading && searchChannels.length === 0
                ? Array.from({ length: 5 }).map((_, i) => (
                    <div key={`ch-sk-${i}`} className="flex items-center gap-4 px-4 py-3.5 sm:py-4 animate-pulse">
                      <div className="w-[52px] h-[52px] sm:w-14 sm:h-14 rounded-full bg-brand-800/80 shrink-0" />
                      <div className="flex-1 space-y-2 min-w-0 py-0.5">
                        <div className="h-4 bg-brand-800/80 rounded-md max-w-[200px]" />
                        <div className="h-3 bg-brand-800/60 rounded-md max-w-[140px]" />
                      </div>
                    </div>
                  ))
                : null}
              {searchChannels.map((ch) => {
                const login = ch.broadcaster_login;
                if (!login) return null;
                const nameForLive = ch.display_name || login;
                const live = !!ch.is_live || !!liveMap[login] || !!liveMap[nameForLive];
                return (
                  <Link
                    key={ch.id || login}
                    to={`/channel/${encodeURIComponent(login)}`}
                    className="flex items-center gap-4 px-4 py-3.5 sm:py-4 hover:bg-brand-800/45 transition-colors group text-left"
                  >
                    <img
                      src={
                        ch.thumbnail_url ||
                        `https://api.dicebear.com/7.x/avataaars/svg?seed=${encodeURIComponent(login)}`
                      }
                      alt=""
                      className="w-[52px] h-[52px] sm:w-14 sm:h-14 rounded-full object-cover border border-brand-700/60 bg-brand-900 shrink-0"
                    />
                    <div className="min-w-0 flex-1">
                      <div className="flex items-center gap-2 flex-wrap">
                        <span className="font-semibold text-white group-hover:text-brand-200 transition-colors">
                          {ch.display_name || login}
                        </span>
                        {live ? (
                          <span className="text-[10px] font-bold uppercase tracking-wide text-red-400 px-1.5 py-0.5 rounded bg-red-500/15 border border-red-500/30">
                            В эфире
                          </span>
                        ) : null}
                      </div>
                      <p className="text-sm text-gray-500 truncate">@{login}</p>
                      <p className="text-[11px] text-gray-600 mt-0.5 hidden sm:block">
                        Страница канала и клипы на ClipStream
                      </p>
                    </div>
                    <ChevronRight
                      className="w-5 h-5 text-gray-600 group-hover:text-brand-400 shrink-0 hidden sm:block"
                      strokeWidth={2}
                    />
                  </Link>
                );
              })}
            </div>
          ) : null}
        </section>
      )}

      {showSearchUsersBlock && (
        <section className="mb-7 space-y-2.5" aria-labelledby="search-users-heading">
          <h2 id="search-users-heading" className="text-sm font-semibold text-gray-400 uppercase tracking-wide">
            Профили ClipStream
          </h2>
          <p className="text-[11px] text-gray-600 -mt-1">Поиск профиля — от 2 символов</p>
          <div className="rounded-xl border border-brand-700/55 bg-brand-900/35 overflow-hidden divide-y divide-brand-800/70">
            {searchMetaLoading && searchUsers.length === 0
              ? Array.from({ length: 4 }).map((_, i) => (
                  <div key={`u-sk-${i}`} className="flex items-center gap-4 px-4 py-3.5 sm:py-4 animate-pulse">
                    <div className="w-12 h-12 rounded-full bg-brand-800/80 shrink-0" />
                    <div className="flex-1 space-y-2 min-w-0">
                      <div className="h-4 bg-brand-800/80 rounded-md max-w-[160px]" />
                      <div className="h-3 bg-brand-800/60 rounded-md max-w-[220px]" />
                    </div>
                  </div>
                ))
              : null}
            {searchUsers.map((u) => (
              <Link
                key={u.id}
                to={`/u/${encodeURIComponent(u.username)}`}
                className="flex items-center gap-4 px-4 py-3.5 sm:py-4 hover:bg-brand-800/45 transition-colors group text-left"
              >
                <img
                  src={
                    u.avatar_url ||
                    `https://api.dicebear.com/7.x/avataaars/svg?seed=${encodeURIComponent(u.username || 'u')}`
                  }
                  alt=""
                  className="w-12 h-12 rounded-full object-cover border border-brand-700/50 bg-brand-900 shrink-0"
                />
                <div className="min-w-0 flex-1">
                  <p className="font-semibold text-white group-hover:text-cyan-200/90 transition-colors truncate">
                    @{u.username}
                  </p>
                  <p className="text-xs text-gray-500">Публичный профиль на ClipStream</p>
                </div>
                <ChevronRight
                  className="w-5 h-5 text-gray-600 group-hover:text-cyan-400/80 shrink-0 hidden sm:block"
                  strokeWidth={2}
                />
              </Link>
            ))}
          </div>
        </section>
      )}

      {q && (
        <section
          className="mb-5 rounded-xl border border-brand-700/55 bg-brand-900/25 overflow-hidden"
          aria-labelledby="search-clips-heading"
        >
          <button
            type="button"
            id="search-clips-heading"
            onClick={toggleClipsPanel}
            aria-expanded={clipsPanelOpen}
            aria-controls="search-clips-panel"
            className="w-full flex items-center justify-between gap-3 px-4 py-3.5 text-left hover:bg-brand-800/35 transition-colors"
          >
            <div className="min-w-0">
              <span className="text-sm font-semibold text-white uppercase tracking-wide">Клипы</span>
              <p className="text-[11px] text-gray-500 mt-0.5">
                Каталог по названию, стримеру и создателю
                {!clipsPanelOpen && !loading && !initialLoading ? (
                  <>
                    {' '}
                    · в выдаче{' '}
                    <span className="text-gray-400 tabular-nums">{searchClips.length}</span>
                  </>
                ) : null}
                {!clipsPanelOpen ? ' · свернуто' : ''}
              </p>
            </div>
            <ChevronDown
              className={`w-5 h-5 text-gray-500 shrink-0 transition-transform duration-200 ${
                clipsPanelOpen ? 'rotate-180' : ''
              }`}
              strokeWidth={2}
            />
          </button>
          {clipsPanelOpen ? (
            <div id="search-clips-panel" className="border-t border-brand-800/70">
              <div className="sticky top-0 z-10 px-4 lg:px-6 py-3 bg-brand-900/96 backdrop-blur-md space-y-4">
                <div className="space-y-2">
                  <p className="text-[11px] font-medium text-gray-500 flex items-center gap-1.5 uppercase tracking-wide">
                    <CalendarRange className="w-3.5 h-3.5 text-brand-400 shrink-0" strokeWidth={2} />
                    Сначала выберите период
                  </p>
                  <p className="text-[10px] text-gray-600 leading-snug -mt-0.5">
                    Ограничение по дате создания клипа на Twitch (и минимальные просмотры для этого окна)
                  </p>
                  <div className="flex flex-wrap gap-2">
                    {PERIODS.map(({ key, label }) => (
                      <button
                        key={key}
                        type="button"
                        onClick={() => setPeriod(key)}
                        className={filterChipClass(period === key)}
                      >
                        {label}
                      </button>
                    ))}
                  </div>
                </div>
                <div className="space-y-2 pt-1 border-t border-brand-800/60">
                  <p className="text-[11px] font-medium text-gray-500 flex items-center gap-1.5 uppercase tracking-wide">
                    <ListOrdered className="w-3.5 h-3.5 text-brand-400 shrink-0" strokeWidth={2} />
                    Затем порядок в списке
                  </p>
                  <p className="text-[10px] text-gray-600 leading-snug -mt-0.5">
                    Как упорядочить клипы, уже попавшие в выбранный период
                  </p>
                  <div className="flex flex-wrap gap-2">
                    {SORTS.map(({ key, label, icon: Icon }) => (
                      <button
                        key={key}
                        type="button"
                        onClick={() => setSort(key)}
                        className={`inline-flex items-center gap-1.5 ${filterChipClass(sort === key)}`}
                      >
                        <Icon className="w-3.5 h-3.5 opacity-90 shrink-0" strokeWidth={2} />
                        {label}
                      </button>
                    ))}
                  </div>
                </div>
              </div>
            </div>
          ) : null}
        </section>
      )}

      {q && clipsPanelOpen && searchAppendError && searchClips.length > 0 && (
        <div className="mb-4 rounded-lg border border-amber-600/35 bg-amber-950/20 px-3 py-2 flex flex-wrap items-center gap-3 text-amber-100/90 text-xs">
          <span>Не удалось подгрузить следующие результаты.</span>
          <button
            type="button"
            onClick={retrySearchAppend}
            className="inline-flex items-center gap-1.5 font-semibold text-amber-200 hover:text-white underline underline-offset-2"
          >
            <RefreshCw className="w-3.5 h-3.5" />
            Повторить
          </button>
        </div>
      )}

      {q && clipsPanelOpen && searchFeedError && !loading && (
        <div className="mb-6 rounded-xl border border-amber-600/40 bg-amber-950/30 p-5 flex flex-col sm:flex-row sm:items-center gap-4 text-amber-100/90 text-sm">
          <AlertTriangle className="w-5 h-5 text-amber-400 shrink-0" />
          <p className="flex-1">Не удалось загрузить поиск. Проверьте соединение и попробуйте снова — иначе кажется, что роликов просто нет.</p>
          <button
            type="button"
            onClick={() => fetchSearchPage(0, true)}
            className="inline-flex items-center justify-center gap-2 px-4 py-2 rounded-lg bg-amber-500/20 border border-amber-500/50 text-amber-100 font-semibold hover:bg-amber-500/30 transition-colors shrink-0"
          >
            <RefreshCw className="w-4 h-4" />
            Повторить
          </button>
        </div>
      )}

      {searchTotallyEmpty && (
        <div className="rounded-xl border border-brand-700/50 bg-brand-800/40 p-8 text-center text-sm text-gray-400 mb-6">
          <Search className="w-8 h-8 mx-auto mb-3 text-gray-500 opacity-80" />
          <p className="text-white font-medium mb-1">Ничего не найдено</p>
          <p className="text-xs text-gray-500 max-w-md mx-auto">
            Нет клипов в каталоге, каналов Twitch и профилей по этому запросу. Попробуйте другое написание или снимите
            жёсткие фильтры периода для клипов.
          </p>
        </div>
      )}

      {q &&
        clipsPanelOpen &&
        searchClips.length === 0 &&
        !loading &&
        !initialLoading &&
        !searchFeedError &&
        (searchChannels.length > 0 || searchUsers.length > 0) && (
          <p className="text-sm text-gray-500 mb-4 flex items-start gap-2">
            <Search className="w-4 h-4 text-gray-600 shrink-0 mt-0.5" />
            <span>
              В каталоге клипов совпадений нет — выше показаны каналы и профили. Смените период (например «Все время») или
              сортировку.
            </span>
          </p>
        )}

      {q ? (
        clipsPanelOpen ? (
          <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 xl:grid-cols-4 gap-4 lg:gap-5">
            {searchClips.map((clip, i) => {
              const isLast = i === searchClips.length - 1;
              return (
                <div key={clip.id} ref={isLast ? lastRef : null}>
                  <ClipCard clip={clip} index={i} onClick={goClip} isLive={!!liveMap[clip.broadcaster_name]} highlight={q} />
                </div>
              );
            })}
            {(loading || initialLoading) && Array.from({ length: initialLoading ? 12 : 4 }).map((_, i) => (
              <div key={`skeleton-${i}`} className="bg-brand-800/40 rounded-xl overflow-hidden border border-brand-700/40 animate-pulse">
                <div className="aspect-video bg-brand-700/40" />
                <div className="p-3 space-y-2">
                  <div className="h-3.5 bg-brand-700/40 rounded w-[85%]" />
                  <div className="h-3.5 bg-brand-700/40 rounded w-[60%]" />
                  <div className="h-3 bg-brand-700/30 rounded w-[45%]" />
                </div>
              </div>
            ))}
          </div>
        ) : (
          <p className="text-sm text-gray-500 mb-6 px-1">
            Блок клипов свернут. Нажмите заголовок «Клипы» выше, чтобы снова увидеть фильтры и сетку.
          </p>
        )
      ) : (
        <div className="space-y-10 pb-16">
          <header className="border-b border-brand-800/80 pb-5">
            <div className="flex items-center gap-3">
              <div className="w-10 h-10 rounded-xl bg-brand-800 border border-brand-700/50 flex items-center justify-center">
                <HomeIcon className="w-5 h-5 text-brand-400" />
              </div>
              <div>
                <h1 className="text-xl sm:text-2xl font-bold text-white tracking-tight">Главная</h1>
                <p className="text-xs sm:text-sm text-gray-500 mt-0.5">
                  Рекомендации по вашим просмотрам, подпискам на сайте и похожей аудитории; тренды, новинки и
                  иногда случайные «открытия» для разнообразия.
                </p>
              </div>
            </div>
          </header>

          {!isAuth && (
            <div className="rounded-xl border border-[#9146FF]/30 bg-[#9146FF]/10 px-4 py-3 text-sm text-gray-300 leading-relaxed">
              <span className="font-semibold text-white">Войдите через Twitch</span>
              {' — '}
              чтобы учитывались просмотры для персональной ленты, появились «Друзья смотрят», блок «Новое у каналов,
              где вы были», подписки в боковой панели, лайки и комментарии с вашим профилем на ClipStream.
            </div>
          )}

          {channelUpdateClips.length > 0 && (
            <section className="space-y-3">
              <div className="flex items-end justify-between gap-2">
                <h2 className="text-base font-bold text-white flex items-center gap-2">
                  <Sparkles className="w-4 h-4 text-emerald-400 shrink-0" />
                  Новое у каналов, где вы были
                </h2>
              </div>
              <p className="text-[11px] text-gray-500 -mt-1">
                Свежие клипы (около недели) от стримеров, чьи ролики вы недавно открывали на ClipStream.
              </p>
              <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 xl:grid-cols-4 gap-x-4 gap-y-6">
                {channelUpdateClips.map((clip, i) => (
                  <div key={`chup-${clip.id}`}>
                    <ClipCard
                      clip={clip}
                      index={i}
                      onClick={goClip}
                      isLive={!!liveMap[clip.broadcaster_name]}
                    />
                  </div>
                ))}
              </div>
            </section>
          )}

          {friendsClips.length > 0 && (
            <section className="space-y-3">
              <div className="flex items-end justify-between gap-2">
                <h2 className="text-base font-bold text-white flex items-center gap-2">
                  <Users className="w-4 h-4 text-cyan-400 shrink-0" />
                  Друзья смотрят
                </h2>
              </div>
              <p className="text-[11px] text-gray-500 -mt-1">
                Недавние просмотры людей, на которых вы подписаны в ClipStream.
              </p>
              <div className="flex gap-3 overflow-x-auto pb-2 snap-x snap-mandatory scrollbar-thin -mx-1 px-1">
                {friendsClips.map((clip, i) => (
                  <div key={`friends-${clip.id}`} className="min-w-[min(280px,85vw)] max-w-[280px] snap-start shrink-0">
                    <ClipCard
                      clip={clip}
                      index={i}
                      onClick={goClip}
                      isLive={!!liveMap[clip.broadcaster_name]}
                    />
                  </div>
                ))}
              </div>
            </section>
          )}

          {discussedClips.length > 0 && (
            <section className="space-y-3">
              <div className="flex items-end justify-between gap-2">
                <h2 className="text-base font-bold text-white flex items-center gap-2">
                  <MessageCircle className="w-4 h-4 text-fuchsia-400 shrink-0" />
                  Обсуждают сейчас
                </h2>
              </div>
              <p className="text-[11px] text-gray-500 -mt-1">
                Клипы с самым живым комментированием за последние 24 часа на ClipStream.
              </p>
              <div className="flex gap-3 overflow-x-auto pb-2 snap-x snap-mandatory scrollbar-thin -mx-1 px-1">
                {discussedClips.map((clip, i) => (
                  <div key={`discussed-${clip.id}`} className="min-w-[min(280px,85vw)] max-w-[280px] snap-start shrink-0">
                    <ClipCard
                      clip={clip}
                      index={i}
                      onClick={goClip}
                      isLive={!!liveMap[clip.broadcaster_name]}
                    />
                  </div>
                ))}
              </div>
            </section>
          )}

          <section className="space-y-5 border-t border-brand-800/70 pt-10 mt-2">
            <header className="flex items-start gap-3">
              <div className="w-10 h-10 rounded-xl bg-brand-800/80 border border-brand-700/50 flex items-center justify-center shrink-0">
                <Rows3 className="w-5 h-5 text-brand-400" />
              </div>
              <div className="min-w-0">
                <h2 className="text-base sm:text-lg font-bold text-white tracking-tight">Главная лента</h2>
                <p className="text-xs sm:text-sm text-gray-500 mt-1 leading-relaxed max-w-2xl">
                  Рекомендации, тренды и новинки в одном потоке. Листайте страницу вниз — следующие карточки
                  подгрузятся сами.
                </p>
              </div>
            </header>
            <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 xl:grid-cols-4 gap-x-4 gap-y-9">
              {showHomeSkeleton &&
                Array.from({ length: 12 }).map((_, i) => (
                  <div
                    key={`feed-sk-${i}`}
                    className="bg-brand-900/50 rounded-xl overflow-hidden border border-brand-800/60 animate-pulse"
                  >
                    <div className="aspect-video bg-brand-800/60" />
                    <div className="p-3 pt-3 space-y-2">
                      <div className="flex gap-3">
                        <div className="w-9 h-9 rounded-full bg-brand-800/80 shrink-0" />
                        <div className="flex-1 space-y-2 pt-0.5">
                          <div className="h-3.5 bg-brand-800/70 rounded w-full" />
                          <div className="h-3 bg-brand-800/50 rounded w-2/3" />
                        </div>
                      </div>
                    </div>
                  </div>
                ))}
              {!showHomeSkeleton &&
                feedRows.map((row, i) => {
                  const isLast = i === feedRows.length - 1;
                  return (
                    <div key={`feed-${row.clip.id}`} ref={isLast ? lastFeedRef : null}>
                      <ClipCard
                        clip={row.clip}
                        shelfKey={row.shelf}
                        index={i}
                        onClick={goClip}
                        isLive={!!liveMap[row.clip.broadcaster_name]}
                      />
                    </div>
                  );
                })}
              {!showHomeSkeleton &&
                feedLoading &&
                Array.from({ length: 4 }).map((_, i) => (
                  <div
                    key={`feed-more-sk-${i}`}
                    className="bg-brand-900/50 rounded-xl overflow-hidden border border-brand-800/60 animate-pulse"
                  >
                    <div className="aspect-video bg-brand-800/60" />
                    <div className="p-3 space-y-2">
                      <div className="h-3.5 bg-brand-800/70 rounded w-[88%]" />
                      <div className="h-3 bg-brand-800/50 rounded w-[55%]" />
                    </div>
                  </div>
                ))}
            </div>

            {feedLoadMoreError && (
              <div className="rounded-xl border border-amber-600/40 bg-amber-950/25 px-4 py-3 flex flex-col sm:flex-row sm:items-center gap-3 text-sm text-amber-100/90">
                <AlertTriangle className="w-5 h-5 text-amber-400 shrink-0" />
                <p className="flex-1">Не удалось подгрузить следующую часть ленты.</p>
                <button
                  type="button"
                  onClick={retryLoadMoreFeed}
                  className="inline-flex items-center justify-center gap-2 px-3 py-2 rounded-lg bg-amber-500/20 border border-amber-500/45 text-amber-100 font-medium hover:bg-amber-500/30 shrink-0"
                >
                  <RefreshCw className="w-4 h-4" />
                  Повторить
                </button>
              </div>
            )}
          </section>

          {!showHomeSkeleton && !loading && !homeFeedError && feedRows.length > 0 && !feedHasMore && (
            <section className="rounded-2xl border border-brand-700/50 bg-brand-900/40 px-5 py-6 text-center space-y-4">
              <p className="text-white font-semibold">Вы дошли до конца этой порции ленты</p>
              <p className="text-sm text-gray-500 max-w-lg mx-auto">
                Чтобы не обрывать сессию — откройте тренды, категории или вертикальную ленту: там другой порядок
                клипов.
              </p>
              <div className="flex flex-wrap justify-center gap-2 pt-1">
                <button
                  type="button"
                  onClick={() => navigate('/trends')}
                  className="inline-flex items-center gap-2 px-4 py-2 rounded-xl bg-amber-500/15 border border-amber-500/40 text-amber-200 text-sm font-medium hover:bg-amber-500/25"
                >
                  <Flame className="w-4 h-4" />
                  Тренды
                </button>
                <button
                  type="button"
                  onClick={() => navigate('/categories')}
                  className="inline-flex items-center gap-2 px-4 py-2 rounded-xl bg-brand-800 border border-brand-600/50 text-gray-200 text-sm font-medium hover:bg-brand-700"
                >
                  <Compass className="w-4 h-4" />
                  Категории
                </button>
                <button
                  type="button"
                  onClick={() => navigate('/feed')}
                  className="inline-flex items-center gap-2 px-4 py-2 rounded-xl bg-brand-800 border border-brand-600/50 text-gray-200 text-sm font-medium hover:bg-brand-700"
                >
                  <Rows3 className="w-4 h-4" />
                  Лента
                </button>
                <button
                  type="button"
                  onClick={goRandomClip}
                  className="inline-flex items-center gap-2 px-4 py-2 rounded-xl bg-violet-500/15 border border-violet-500/40 text-violet-200 text-sm font-medium hover:bg-violet-500/25"
                >
                  <Dices className="w-4 h-4" />
                  Случайный клип
                </button>
              </div>
            </section>
          )}

          {!showHomeSkeleton && !loading && !homeFeedError && feedRows.length === 0 && (
            <p className="text-center text-sm text-gray-500 py-10 border border-dashed border-brand-700/50 rounded-xl">
              Пока нет клипов в ленте (или всё отфильтровано). Зайдите позже или откройте категории и поиск.
            </p>
          )}

          {similarChannels.length > 0 && (
            <section className="space-y-3 border-t border-brand-800/60 pt-8">
              <h2 className="text-base font-bold text-white flex items-center gap-2">
                <Users className="w-4 h-4 text-emerald-400" />
                Каналы для вас
              </h2>
              <div className="flex gap-3 overflow-x-auto pb-2 -mx-1 px-1">
                {similarChannels.map((ch) => (
                  <button
                    key={ch.broadcaster_id}
                    type="button"
                    onClick={() => navigate(`/channel/${encodeURIComponent(ch.broadcaster_name || ch.broadcaster_id)}`)}
                    className="min-w-[220px] max-w-[240px] shrink-0 text-left p-3 rounded-xl border border-brand-700/50 bg-brand-900/40 hover:bg-brand-800/60 transition-colors"
                  >
                    <div className="flex items-center gap-2.5">
                      <img
                        src={ch.broadcaster_avatar || `https://api.dicebear.com/7.x/avataaars/svg?seed=${ch.broadcaster_id || 'streamer'}`}
                        alt=""
                        className="w-9 h-9 rounded-full border border-brand-700/60 bg-brand-900"
                      />
                      <div className="min-w-0">
                        <p className="text-sm font-semibold text-white truncate">{ch.broadcaster_name || ch.broadcaster_id}</p>
                        <p className="text-[11px] text-gray-500 truncate">{ch.game_name || 'Разные категории'}</p>
                      </div>
                    </div>
                  </button>
                ))}
              </div>
            </section>
          )}
        </div>
      )}
    </div>
  );
}
