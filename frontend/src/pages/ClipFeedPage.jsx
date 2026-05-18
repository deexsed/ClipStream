import { useCallback, useEffect, useLayoutEffect, useRef, useState } from 'react';
import { useSearchParams, useNavigate } from 'react-router-dom';
import { ChevronDown, ChevronUp, Flame, Compass, Rows3, RefreshCw, AlertTriangle, Dices } from 'lucide-react';
import { clips as clipsApi } from '../api/client';
import ClipViewerPanel from '../components/clips/ClipViewerPanel';
import useAuthStore from '../store/useAuthStore';
import { useClipLangStore } from '../store/useClipLangStore';

const SLIDE_H = 'min-h-[calc(100dvh-4rem)] h-[calc(100dvh-4rem)]';

function FeedNavButtons({ activeIndex, clipCount, onNavigate, variant = 'vertical', size = 'md' }) {
  const lg = size === 'lg';
  const btnClass = lg
    ? 'flex h-[3.35rem] w-[3.35rem] items-center justify-center rounded-full bg-white/[0.08] text-white shadow-lg shadow-black/40 backdrop-blur-md transition-colors hover:bg-white/[0.14] active:scale-[0.97] disabled:opacity-25 disabled:pointer-events-none'
    : 'flex h-10 w-10 items-center justify-center rounded-full bg-white/[0.08] text-white shadow-md backdrop-blur-sm transition-colors hover:bg-white/[0.14] disabled:opacity-30';
  const iconClass = lg ? 'w-8 h-8' : 'w-5 h-5';
  const stroke = lg ? 2.5 : 2.25;
  const gap = lg ? 'gap-4' : 'gap-2';
  return (
    <div
      className={
        variant === 'horizontal' ? `flex flex-row items-center ${gap}` : `flex flex-col items-center ${gap}`
      }
    >
      <button
        type="button"
        className={btnClass}
        disabled={activeIndex <= 0}
        onClick={() => onNavigate(Math.max(activeIndex - 1, 0))}
        aria-label="Предыдущий клип"
      >
        <ChevronUp className={iconClass} strokeWidth={stroke} />
      </button>
      <button
        type="button"
        className={btnClass}
        disabled={activeIndex >= clipCount - 1}
        onClick={() => onNavigate(Math.min(activeIndex + 1, clipCount - 1))}
        aria-label="Следующий клип"
      >
        <ChevronDown className={iconClass} strokeWidth={stroke} />
      </button>
    </div>
  );
}
const WHEEL_COOLDOWN_MS = 240;

export default function ClipFeedPage() {
  const [searchParams] = useSearchParams();
  const navigate = useNavigate();
  const startRaw = searchParams.get('start');
  const startId = startRaw != null && startRaw !== '' ? Number(startRaw) : null;
  const startValid = startId != null && Number.isFinite(startId) && startId > 0;

  const { isAuth } = useAuthStore();
  const clipLangKey = useClipLangStore((s) => s.codes.join(','));

  const [clips, setClips] = useState([]);
  const [loading, setLoading] = useState(true);
  const [feedError, setFeedError] = useState(false);
  const [feedLoading, setFeedLoading] = useState(false);
  const [feedHasMore, setFeedHasMore] = useState(true);
  const [feedMoreError, setFeedMoreError] = useState(false);
  const [activeIndex, setActiveIndex] = useState(0);

  const scrollRef = useRef(null);
  const sentinelRef = useRef(null);
  const activeIndexRef = useRef(0);
  const clipsLenRef = useRef(0);
  const wheelCooldownRef = useRef(0);

  activeIndexRef.current = activeIndex;
  clipsLenRef.current = clips.length;
  const homeSessionRef = useRef(null);
  const seenClipIdsRef = useRef(new Set());
  const feedFetchGenRef = useRef(0);

  if (homeSessionRef.current === null) {
    homeSessionRef.current =
      typeof crypto !== 'undefined' && typeof crypto.randomUUID === 'function'
        ? crypto.randomUUID()
        : `feed_${Date.now()}_${Math.random().toString(36).slice(2, 12)}`;
  }

  const mergeUnique = useCallback((prev, rows) => {
    const seen = new Set(prev.map((c) => c.id));
    const next = [...prev];
    for (const row of rows) {
      const c = row.clip;
      if (!c || !c.id || seen.has(c.id)) continue;
      seen.add(c.id);
      seenClipIdsRef.current.add(c.id);
      next.push(c);
    }
    return next;
  }, []);

  useEffect(() => {
    const gen = ++feedFetchGenRef.current;
    setLoading(true);
    setFeedError(false);
    setFeedMoreError(false);
    seenClipIdsRef.current = new Set();
    setClips([]);
    setActiveIndex(0);
    setFeedHasMore(true);

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
        let list = rows.map((r) => r.clip);
        if (startValid) {
          const without = list.filter((c) => c.id !== startId);
          const starter = list.find((c) => c.id === startId);
          list = starter ? [starter, ...without] : [{ id: startId, thumbnail_url: null }, ...without];
          if (!starter) seenClipIdsRef.current.add(startId);
        }
        setClips(list);
        setFeedHasMore(items.length >= 18);
      })
      .catch(() => {
        if (gen !== feedFetchGenRef.current) return;
        setFeedError(true);
        setClips([]);
        setFeedHasMore(false);
      })
      .finally(() => {
        if (gen !== feedFetchGenRef.current) return;
        setLoading(false);
      });
  }, [clipLangKey, isAuth, startValid, startId]);

  const loadMoreFeed = useCallback((opts) => {
    const force = opts && opts.force;
    if (feedLoading || (!feedHasMore && !force)) return;
    if (force) setFeedMoreError(false);
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
        setClips((prev) => mergeUnique(prev, items));
        setFeedHasMore(items.length >= 15);
      })
      .catch(() => {
        setFeedMoreError(true);
        setFeedHasMore(false);
      })
      .finally(() => setFeedLoading(false));
  }, [feedLoading, feedHasMore, mergeUnique]);

  const retryLoadMoreFeed = useCallback(() => {
    setFeedHasMore(true);
    loadMoreFeed({ force: true });
  }, [loadMoreFeed]);

  const goRandomClip = useCallback(() => {
    clipsApi
      .random()
      .then((data) => {
        const id = data.clip?.id;
        if (id) navigate(`/clip/${id}`);
      })
      .catch(() => {});
  }, [navigate]);

  useEffect(() => {
    const root = scrollRef.current;
    const sentinel = sentinelRef.current;
    if (!root || !sentinel || !feedHasMore || feedLoading) return;
    const io = new IntersectionObserver(
      (entries) => {
        if (entries[0]?.isIntersecting) loadMoreFeed();
      },
      { root, rootMargin: '480px', threshold: 0 },
    );
    io.observe(sentinel);
    return () => io.disconnect();
  }, [feedHasMore, feedLoading, loadMoreFeed, clips.length]);

  useLayoutEffect(() => {
    const root = scrollRef.current;
    if (!root || clips.length === 0) return;
    const slides = root.querySelectorAll('[data-feed-slide]');
    if (slides.length === 0) return;

    const obs = new IntersectionObserver(
      (entries) => {
        const visible = entries.filter((e) => e.isIntersecting && e.intersectionRatio >= 0.45);
        if (visible.length === 0) return;
        visible.sort((a, b) => b.intersectionRatio - a.intersectionRatio);
        const idx = Number(visible[0].target.getAttribute('data-feed-slide'));
        if (!Number.isNaN(idx)) setActiveIndex(idx);
      },
      { root, threshold: [0, 0.25, 0.45, 0.6, 0.75, 1] },
    );
    slides.forEach((el) => obs.observe(el));
    return () => obs.disconnect();
  }, [clips]);

  const scrollToIndex = useCallback((i) => {
    const root = scrollRef.current;
    if (!root) return;
    const el = root.querySelector(`[data-feed-slide="${i}"]`);
    el?.scrollIntoView({ behavior: 'smooth', block: 'start' });
  }, []);

  useEffect(() => {
    const root = scrollRef.current;
    if (!root || clips.length === 0) return;

    const goNext = () => {
      const i = activeIndexRef.current;
      const n = clipsLenRef.current;
      const next = Math.min(i + 1, n - 1);
      if (next !== i) scrollToIndex(next);
    };
    const goPrev = () => {
      const i = activeIndexRef.current;
      const prev = Math.max(i - 1, 0);
      if (prev !== i) scrollToIndex(prev);
    };

    const tryNavigate = (deltaY) => {
      const now = Date.now();
      if (now - wheelCooldownRef.current < WHEEL_COOLDOWN_MS) return;
      wheelCooldownRef.current = now;
      if (deltaY > 0) goNext();
      else if (deltaY < 0) goPrev();
    };

    const onWheel = (e) => {
      const local = e.target.closest('[data-feed-local-scroll]');
      if (local) {
        const delta = e.deltaY;
        const { scrollTop, scrollHeight, clientHeight } = local;
        const canUp = scrollTop > 1;
        const canDown = scrollTop + clientHeight < scrollHeight - 2;
        if (delta < 0 && canUp) return;
        if (delta > 0 && canDown) return;
        e.preventDefault();
        if (Math.abs(delta) < 1) return;
        tryNavigate(delta);
        return;
      }

      if (Math.abs(e.deltaY) < 2) return;
      e.preventDefault();
      tryNavigate(e.deltaY);
    };

    root.addEventListener('wheel', onWheel, { passive: false });
    return () => root.removeEventListener('wheel', onWheel);
  }, [clips.length, scrollToIndex]);

  useEffect(() => {
    if (clips.length === 0) return;

    const isTyping = (t) => {
      const tag = t?.tagName;
      return tag === 'TEXTAREA' || tag === 'INPUT' || tag === 'SELECT' || t?.isContentEditable;
    };

    const onKey = (e) => {
      const t = e.target;
      if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
        if (isTyping(t)) return;
        e.preventDefault();
        const i = activeIndexRef.current;
        const n = clipsLenRef.current;
        if (e.key === 'ArrowDown') {
          const next = Math.min(i + 1, n - 1);
          if (next !== i) scrollToIndex(next);
        } else {
          const prev = Math.max(i - 1, 0);
          if (prev !== i) scrollToIndex(prev);
        }
        return;
      }
      if (e.key === ' ' || e.code === 'Space') {
        if (isTyping(t) || t?.tagName === 'BUTTON' || t?.tagName === 'A') return;
        e.preventDefault();
        const i = activeIndexRef.current;
        const n = clipsLenRef.current;
        const next = Math.min(i + 1, n - 1);
        if (next !== i) scrollToIndex(next);
      }
    };

    window.addEventListener('keydown', onKey, true);
    return () => window.removeEventListener('keydown', onKey, true);
  }, [clips.length, scrollToIndex]);

  const origin = typeof window !== 'undefined' ? window.location.origin : '';

  if (loading && clips.length === 0) {
    return (
      <div className="flex items-center justify-center h-[calc(100dvh-4rem)]">
        <div className="flex gap-2">
          <div className="w-3 h-3 bg-brand-500 rounded-full animate-bounce" />
          <div className="w-3 h-3 bg-brand-500 rounded-full animate-bounce" style={{ animationDelay: '0.1s' }} />
          <div className="w-3 h-3 bg-brand-500 rounded-full animate-bounce" style={{ animationDelay: '0.2s' }} />
        </div>
      </div>
    );
  }

  if (feedError && clips.length === 0) {
    return (
      <div className="flex flex-col items-center justify-center h-[calc(100dvh-4rem)] text-gray-400 text-sm px-4">
        Не удалось загрузить ленту. Попробуйте обновить страницу.
      </div>
    );
  }

  const feedNavDesktop =
    clips.length > 0 ? (
      <FeedNavButtons
        activeIndex={activeIndex}
        clipCount={clips.length}
        onNavigate={scrollToIndex}
        size="lg"
      />
    ) : null;

  const feedNavMobile =
    clips.length > 0 ? (
      <div className="flex justify-center py-2">
        <FeedNavButtons
          activeIndex={activeIndex}
          clipCount={clips.length}
          onNavigate={scrollToIndex}
          variant="horizontal"
        />
      </div>
    ) : null;

  return (
    <div
      ref={scrollRef}
      role="region"
      aria-label="Вертикальная лента клипов"
      tabIndex={-1}
      className="relative h-[calc(100dvh-4rem)] overflow-y-auto overflow-x-hidden snap-y snap-mandatory outline-none"
    >
      <p className="sr-only">
        Колёсико мыши переключает клип; в открытой правой панели сначала прокручивается текст. Клавиши: стрелки вверх и
        вниз и пробел — следующий клип. На странице клипа можно включить автопереход к следующему ролику из колонки
        «Похожее».
      </p>

      {clips.map((c, i) => {
        const near = Math.abs(i - activeIndex) <= 1;
        const isCurrent = i === activeIndex;
        return (
          <section
            key={c.id}
            data-feed-slide={i}
            className={`snap-start shrink-0 ${SLIDE_H} flex flex-col transition-shadow duration-300 ${
              isCurrent
                ? 'bg-[#06060a] shadow-[inset_0_0_0_1px_rgba(139,92,246,0.12),0_0_40px_-12px_rgba(99,102,241,0.25)]'
                : 'bg-[#07090f]'
            }`}
          >
            <div className="flex-1 min-h-0 flex flex-col">
              {near ? (
                <ClipViewerPanel
                  clipId={String(c.id)}
                  isActive={isCurrent}
                  recordViewMode="whenActive"
                  showPlayNextToggle={false}
                  showBreadcrumbs={false}
                  embedOnlyWhenActive
                  compact
                  layoutVariant="feedDesktop"
                  className="flex-1 min-h-0"
                  shareUrl={`${origin}/clip/${c.id}`}
                  onFetchError={() => {}}
                  feedNavDesktop={isCurrent ? feedNavDesktop : null}
                  feedNavMobile={isCurrent ? feedNavMobile : null}
                />
              ) : (
                <div className="flex-1 min-h-0 flex flex-col lg:flex-row gap-4 items-center justify-center p-6 lg:p-8">
                  {c.thumbnail_url ? (
                    <img
                      src={c.thumbnail_url}
                      alt=""
                      className="w-full max-w-[min(100%,520px)] lg:w-[38%] lg:max-w-[480px] aspect-video object-cover rounded-xl border border-brand-800/50 opacity-50"
                    />
                  ) : (
                    <div className="w-full max-w-md aspect-video rounded-xl bg-brand-900/60 border border-brand-800/50 animate-pulse" />
                  )}
                  <div className="hidden lg:block flex-1 max-w-md h-32 rounded-xl bg-brand-900/40 border border-brand-800/40 animate-pulse" />
                </div>
              )}
            </div>
          </section>
        );
      })}

      <div ref={sentinelRef} className="h-2 w-full shrink-0" aria-hidden />
      {feedLoading && (
        <div className="flex justify-center py-6 text-gray-500 text-sm">Загрузка ленты…</div>
      )}

      {feedMoreError && (
        <div className="snap-start min-h-[min(40vh,320px)] flex items-center justify-center px-4 py-10 border-t border-brand-800/60 bg-[#07090f]">
          <div className="max-w-md w-full rounded-xl border border-amber-600/40 bg-amber-950/25 p-5 text-center space-y-4 text-sm text-amber-100/90">
            <AlertTriangle className="w-8 h-8 text-amber-400 mx-auto" />
            <p>Не удалось подгрузить ленту дальше.</p>
            <button
              type="button"
              onClick={retryLoadMoreFeed}
              className="inline-flex items-center justify-center gap-2 px-4 py-2 rounded-lg bg-amber-500/20 border border-amber-500/50 text-amber-100 font-semibold hover:bg-amber-500/30"
            >
              <RefreshCw className="w-4 h-4" />
              Повторить
            </button>
          </div>
        </div>
      )}

      {!feedHasMore && !feedLoading && clips.length > 0 && !feedMoreError && (
        <div className="snap-start min-h-[min(50vh,420px)] flex flex-col items-center justify-center px-4 py-12 border-t border-brand-800/60 bg-[#06060a] text-center gap-4">
          <p className="text-white font-semibold text-base">Вы просмотрели эту порцию ленты</p>
          <p className="text-sm text-gray-500 max-w-md">
            Загляните в тренды и категории или начните ленту сначала — так сессия не обрывается на пустом экране.
          </p>
          <div className="flex flex-wrap justify-center gap-2 pt-2">
            <button
              type="button"
              onClick={() => navigate('/trends')}
              className="inline-flex items-center gap-2 px-4 py-2.5 rounded-xl bg-amber-500/15 border border-amber-500/40 text-amber-200 text-sm font-medium hover:bg-amber-500/25"
            >
              <Flame className="w-4 h-4" />
              Тренды
            </button>
            <button
              type="button"
              onClick={() => navigate('/categories')}
              className="inline-flex items-center gap-2 px-4 py-2.5 rounded-xl bg-brand-800 border border-brand-600/50 text-gray-200 text-sm font-medium hover:bg-brand-700"
            >
              <Compass className="w-4 h-4" />
              Категории
            </button>
            <button
              type="button"
              onClick={() => navigate('/')}
              className="inline-flex items-center gap-2 px-4 py-2.5 rounded-xl bg-brand-800 border border-brand-600/50 text-gray-200 text-sm font-medium hover:bg-brand-700"
            >
              <Rows3 className="w-4 h-4" />
              Главная
            </button>
            <button
              type="button"
              onClick={goRandomClip}
              className="inline-flex items-center gap-2 px-4 py-2.5 rounded-xl bg-violet-500/15 border border-violet-500/40 text-violet-200 text-sm font-medium hover:bg-violet-500/25"
            >
              <Dices className="w-4 h-4" />
              Случайный клип
            </button>
          </div>
        </div>
      )}
    </div>
  );
}
