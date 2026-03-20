import { useCallback, useEffect, useRef, useState } from 'react';
import { useNavigate, useSearchParams } from 'react-router-dom';
import { AlertTriangle, Flame, Clock, Timer, Search, Users, Home as HomeIcon } from 'lucide-react';
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

  const liveSource = q ? searchClips : [...friendsClips, ...feedRows.map((r) => r.clip)];
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
    const params = new URLSearchParams({
      sort,
      period,
      limit: '20',
      offset: String(nextOffset),
    });
    if (q) params.set('search', q);
    clipsApi.feed(params.toString())
      .then((data) => {
        const incoming = data.clips || [];
        setSearchClips((prev) => (replace ? incoming : [...prev, ...incoming]));
        setSearchOffset(nextOffset + incoming.length);
        setSearchHasMore(incoming.length >= 20);
      })
      .catch(() => {})
      .finally(() => setLoading(false));
  }, [sort, period, q, clipLangKey]);

  /** Поиск — отдельный эффект. Не смешивать с главной: иначе при смене fetchSearchPage лента главной перезапускалась и могла затереться гонкой. */
  useEffect(() => {
    if (!q) return;
    setSearchClips([]);
    setSearchOffset(0);
    setSearchHasMore(true);
    fetchSearchPage(0, true);
  }, [q, fetchSearchPage]);

  /** Главная: смешанная лента `/clips/home-feed` (быстрее старой тройки запросов + без дублей при подгрузке). */
  useEffect(() => {
    if (q) return;
    const gen = ++feedFetchGenRef.current;
    setHomeFeedError(false);
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
    if (!loading) setInitialLoading(false);
  }, [loading]);

  const loadMore = useCallback(() => {
    if (!q || loading || !searchHasMore) return;
    fetchSearchPage(searchOffset, false);
  }, [q, loading, searchHasMore, fetchSearchPage, searchOffset]);
  const lastRef = useInfiniteScroll(loadMore, searchHasMore, loading);

  const loadMoreFeed = useCallback(() => {
    if (q || feedLoading || !feedHasMore) return;
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
      .catch(() => setFeedHasMore(false))
      .finally(() => setFeedLoading(false));
  }, [q, feedLoading, feedHasMore, clipLangKey]);

  const lastFeedRef = useInfiniteScroll(loadMoreFeed, feedHasMore, feedLoading);

  const goClip = (clip) => navigate(`/clip/${clip.id}`);

  const showHomeSkeleton = !q && (loading || initialLoading) && feedRows.length === 0;

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

      <div className="sticky top-0 z-10 -mx-4 lg:-mx-6 px-4 lg:px-6 py-3 mb-5 bg-brand-900/92 backdrop-blur-md border-y border-brand-700/40 space-y-3">
        {q && <div className="flex flex-wrap gap-2 md:hidden">
          {SORTS.map(({ key, label, icon: Icon }) => (
            <button
              key={key}
              onClick={() => setSort(key)}
              className={`px-3 py-1.5 rounded-full text-sm border transition-colors ${
                sort === key ? 'bg-brand-500 border-brand-500 text-white' : 'bg-brand-800 border-brand-700 text-gray-300 hover:text-white'
              }`}
            >
              <Icon className="w-3.5 h-3.5 inline mr-1" />
              {label}
            </button>
          ))}
        </div>}

        {q && <div className="hidden md:flex flex-wrap gap-2">
          {SORTS.map(({ key, label, icon: Icon }) => (
            <button
              key={key}
              onClick={() => setSort(key)}
              className={`px-3 py-1.5 rounded-full text-sm border transition-colors ${
                sort === key ? 'bg-brand-500 border-brand-500 text-white' : 'bg-brand-800 border-brand-700 text-gray-300 hover:text-white'
              }`}
            >
              <Icon className="w-3.5 h-3.5 inline mr-1" />
              {label}
            </button>
          ))}
        </div>}

        {q && <div className="flex flex-wrap gap-2">
          {PERIODS.map(({ key, label }) => (
            <button
              key={key}
              onClick={() => setPeriod(key)}
              className={`px-3 py-1 rounded-full text-xs border transition-colors ${
                period === key ? 'bg-brand-700 border-brand-600 text-white' : 'bg-brand-900 border-brand-700 text-gray-400 hover:text-gray-200'
              }`}
            >
              {label}
            </button>
          ))}
          {q && (
            <button
              onClick={() => setSearchParams((prev) => {
                const next = new URLSearchParams(prev);
                next.delete('q');
                return next;
              })}
              className="px-3 py-1 rounded-full text-xs border bg-brand-900 border-brand-700 text-gray-300 hover:text-white"
            >
              Сбросить поиск
            </button>
          )}
        </div>}
      </div>

      {q && searchClips.length === 0 && !loading && !initialLoading && (
        <div className="rounded-xl border border-brand-700/50 bg-brand-800/40 p-8 text-center text-sm text-gray-400">
          <Search className="w-5 h-5 mx-auto mb-2 text-gray-500" />
          Ничего не найдено по запросу.
        </div>
      )}

      {q ? (
        <div className="grid grid-cols-1 sm:grid-cols-2 xl:grid-cols-3 2xl:grid-cols-4 gap-4 lg:gap-5">
          {searchClips.map((clip, i) => {
            const isLast = i === searchClips.length - 1;
            return (
              <div key={clip.id} ref={isLast ? lastRef : null} className={i === 0 ? 'sm:col-span-2 xl:col-span-2' : ''}>
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

          <section>
            <h2 className="sr-only">Рекомендуемые клипы</h2>
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
          </section>

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
