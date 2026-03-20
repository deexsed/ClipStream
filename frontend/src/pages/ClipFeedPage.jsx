import { useCallback, useEffect, useLayoutEffect, useRef, useState } from 'react';
import { useSearchParams } from 'react-router-dom';
import { clips as clipsApi } from '../api/client';
import ClipViewerPanel from '../components/clips/ClipViewerPanel';
import useAuthStore from '../store/useAuthStore';
import { useClipLangStore } from '../store/useClipLangStore';

const SLIDE_H = 'min-h-[calc(100dvh-4rem)] h-[calc(100dvh-4rem)]';

export default function ClipFeedPage() {
  const [searchParams] = useSearchParams();
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
  const [activeIndex, setActiveIndex] = useState(0);

  const scrollRef = useRef(null);
  const sentinelRef = useRef(null);
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

  const loadMoreFeed = useCallback(() => {
    if (feedLoading || !feedHasMore) return;
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
      .catch(() => setFeedHasMore(false))
      .finally(() => setFeedLoading(false));
  }, [feedLoading, feedHasMore, mergeUnique]);

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

  const onKeyDown = useCallback(
    (e) => {
      if (e.key !== 'ArrowDown' && e.key !== 'ArrowUp') return;
      const t = e.target;
      const tag = t?.tagName;
      if (tag === 'TEXTAREA' || tag === 'INPUT' || tag === 'SELECT' || t?.isContentEditable) return;

      if (e.key === 'ArrowDown') {
        e.preventDefault();
        const next = Math.min(activeIndex + 1, clips.length - 1);
        if (next !== activeIndex) scrollToIndex(next);
      } else {
        e.preventDefault();
        const prev = Math.max(activeIndex - 1, 0);
        if (prev !== activeIndex) scrollToIndex(prev);
      }
    },
    [activeIndex, clips.length, scrollToIndex],
  );

  useEffect(() => {
    scrollRef.current?.focus({ preventScroll: true });
  }, []);

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

  return (
    <div
      ref={scrollRef}
      role="region"
      aria-label="Вертикальная лента клипов"
      tabIndex={0}
      onKeyDown={onKeyDown}
      className={`h-[calc(100dvh-4rem)] overflow-y-auto overflow-x-hidden snap-y snap-mandatory scroll-smooth outline-none focus-visible:ring-2 focus-visible:ring-brand-500/50 focus-visible:ring-inset`}
    >
      <p className="sr-only">Используйте колёсико мыши или стрелки вверх и вниз для перехода между клипами.</p>

      {clips.map((c, i) => {
        const near = Math.abs(i - activeIndex) <= 1;
        return (
          <section
            key={c.id}
            data-feed-slide={i}
            className={`snap-start shrink-0 ${SLIDE_H} flex flex-col border-b border-brand-800/30`}
          >
            <div className="flex-1 min-h-0 flex flex-col">
              {near ? (
                <div className="flex-1 min-h-0 overflow-y-auto overscroll-y-contain">
                  <ClipViewerPanel
                    clipId={String(c.id)}
                    isActive={i === activeIndex}
                    recordViewMode="whenActive"
                    showPlayNextToggle={false}
                    showBreadcrumbs={false}
                    embedOnlyWhenActive
                    compact
                    shareUrl={`${origin}/clip/${c.id}`}
                    onFetchError={() => {}}
                  />
                </div>
              ) : (
                <div className="flex-1 min-h-0 flex items-center justify-center p-8 bg-[#07090f]">
                  {c.thumbnail_url ? (
                    <img
                      src={c.thumbnail_url}
                      alt=""
                      className="max-w-full max-h-[55vh] object-contain rounded-xl border border-brand-800/50 opacity-60"
                    />
                  ) : (
                    <div className="w-full max-w-md aspect-video rounded-xl bg-brand-900/60 border border-brand-800/50 animate-pulse" />
                  )}
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
    </div>
  );
}
