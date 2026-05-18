import { useState, useEffect, useRef, useCallback } from 'react';
import { Link, useNavigate } from 'react-router-dom';
import { Eye, RefreshCw, Flame, Compass, Rows3 } from 'lucide-react';
import { clips as clipsApi } from '../../api/client';
import { useClipLangStore } from '../../store/useClipLangStore';

function formatDuration(sec) {
  const m = Math.floor(sec / 60);
  const s = Math.floor(sec % 60);
  return `${m}:${String(s).padStart(2, '0')}`;
}

function formatViews(n) {
  if (n >= 1000000) return `${(n / 1000000).toFixed(1)}M`;
  if (n >= 1000) return `${(n / 1000).toFixed(1)}K`;
  return String(n);
}

export default function RecommendedClips({ currentClipId, onQueueChange }) {
  const [recs, setRecs] = useState([]);
  const [busy, setBusy] = useState(false);
  const [listSource, setListSource] = useState('related');
  const [categoryGameId, setCategoryGameId] = useState(0);
  const navigate = useNavigate();
  const clipLangKey = useClipLangStore((s) => s.codes.join(','));
  const mixRef = useRef(0);
  const fallbackOffsetRef = useRef(0);

  const loadFallback = useCallback(
    async (offset = 0) => {
      let gameId = 0;
      try {
        const meta = await clipsApi.get(String(currentClipId));
        gameId = Number(meta.game_id) || 0;
        setCategoryGameId(gameId);
      } catch {
        setCategoryGameId(0);
      }
      const params = new URLSearchParams({
        sort: 'popular',
        period: 'week',
        limit: '20',
        offset: String(offset),
      });
      if (gameId > 0) params.set('game_id', String(gameId));
      const data = await clipsApi.feed(params.toString());
      const raw = (data.clips || []).filter((c) => c.id !== currentClipId);
      return raw.slice(0, 10);
    },
    [currentClipId],
  );

  const runLoad = useCallback(
    async (opts = {}) => {
      const { newMix, excludeIds, fallbackOnly, fallbackShuffle } = opts;
      if (!currentClipId) return;

      if (fallbackOnly) {
        setBusy(true);
        if (fallbackShuffle) {
          fallbackOffsetRef.current = Math.floor(Math.random() * 4) * 15;
        }
        try {
          const list = await loadFallback(fallbackOffsetRef.current);
          setRecs(list);
          setListSource(list.length > 0 ? 'fallback' : 'none');
        } catch {
          setRecs([]);
          setListSource('none');
        } finally {
          setBusy(false);
        }
        return;
      }

      let mix;
      if (newMix != null) {
        mix = newMix;
        mixRef.current = mix;
      } else {
        mix = Math.floor(Math.random() * 1_000_000_000);
        mixRef.current = mix;
      }
      setBusy(true);
      if (newMix != null) setRecs([]);

      const params = new URLSearchParams({
        limit: '32',
        mix: String(mix),
        _: String(Date.now()),
      });
      if (excludeIds?.length) {
        const ex = [...new Set([String(currentClipId), ...excludeIds.map(String)])].join(',');
        params.set('exclude', ex);
      }

      try {
        const data = await clipsApi.related(currentClipId, params.toString());
        const raw = (data.clips || []).filter((c) => c.id !== currentClipId);
        if (raw.length > 0) {
          setRecs(raw.slice(0, 10));
          setListSource('related');
          try {
            const meta = await clipsApi.get(String(currentClipId));
            setCategoryGameId(Number(meta.game_id) || 0);
          } catch {
            setCategoryGameId(0);
          }
        } else {
          fallbackOffsetRef.current = 0;
          const list = await loadFallback(0);
          setRecs(list);
          setListSource(list.length > 0 ? 'fallback' : 'none');
        }
      } catch {
        fallbackOffsetRef.current = 0;
        try {
          const list = await loadFallback(0);
          setRecs(list);
          setListSource(list.length > 0 ? 'fallback' : 'none');
        } catch {
          setRecs([]);
          setListSource('none');
        }
      } finally {
        setBusy(false);
      }
    },
    [currentClipId, loadFallback],
  );

  useEffect(() => {
    mixRef.current = 0;
    fallbackOffsetRef.current = 0;
    setListSource('related');
    void runLoad();
  }, [currentClipId, clipLangKey, runLoad]);

  useEffect(() => {
    onQueueChange?.(recs);
  }, [recs, onQueueChange]);

  const onRefreshClick = () => {
    if (listSource === 'related' && recs.length > 0) {
      void runLoad({ newMix: Math.floor(Math.random() * 1_000_000_000), excludeIds: recs.map((c) => c.id) });
    } else {
      void runLoad({ fallbackOnly: true, fallbackShuffle: true });
    }
  };

  if (!currentClipId) return null;

  const title =
    listSource === 'related'
      ? 'Похожее'
      : listSource === 'fallback'
        ? 'Популярное рядом'
        : 'Куда дальше';
  const subtitle =
    listSource === 'fallback'
      ? 'Похожих почти нет — показываем хиты в этой категории или на сайте.'
      : listSource === 'none'
        ? 'Не удалось подобрать ролики в колонку — откройте ленту или тренды.'
        : null;

  const feedStart = `/feed?start=${encodeURIComponent(String(currentClipId))}`;

  return (
    <div className="rounded-2xl border border-brand-700/45 bg-gradient-to-b from-brand-900/70 to-brand-950/90 p-4 shadow-xl shadow-black/20">
      <div className="flex items-center justify-between gap-2 mb-2">
        <div className="min-w-0">
          <h3 className="font-bold text-white text-sm tracking-tight">{title}</h3>
          {subtitle ? <p className="text-[11px] text-gray-500 mt-1 leading-snug">{subtitle}</p> : null}
        </div>
        <button
          type="button"
          disabled={busy}
          onClick={onRefreshClick}
          className="inline-flex items-center gap-1.5 text-[11px] text-brand-300 hover:text-white px-2 py-1 rounded-lg border border-brand-700/60 bg-brand-950/50 disabled:opacity-40 shrink-0"
        >
          <RefreshCw className={`w-3.5 h-3.5 ${busy ? 'animate-spin' : ''}`} />
          Другие
        </button>
      </div>

      <div className="space-y-1.5 max-h-[min(70vh,520px)] overflow-y-auto pr-1">
        {busy && recs.length === 0 && (
          <div className="space-y-2 py-2">
            {[1, 2, 3].map((i) => (
              <div key={i} className="flex gap-3 p-2.5 animate-pulse">
                <div className="w-28 h-[4.5rem] rounded-lg bg-brand-800/80" />
                <div className="flex-1 space-y-2 py-1">
                  <div className="h-3 bg-brand-800/80 rounded w-full" />
                  <div className="h-3 bg-brand-800/60 rounded w-2/3" />
                </div>
              </div>
            ))}
          </div>
        )}
        {recs.map((clip) => (
          <div
            key={clip.id}
            role="button"
            tabIndex={0}
            onClick={() => navigate(`/clip/${clip.id}`)}
            onKeyDown={(e) => e.key === 'Enter' && navigate(`/clip/${clip.id}`)}
            className="flex gap-3 group cursor-pointer hover:bg-brand-800/55 p-2.5 rounded-xl transition-colors border border-transparent hover:border-brand-600/30"
          >
            <div className="relative w-28 h-[4.5rem] shrink-0 rounded-lg overflow-hidden bg-black ring-1 ring-white/10">
              <img
                src={clip.thumbnail_url}
                className="w-full h-full object-cover opacity-85 group-hover:opacity-100 transition-opacity"
                alt=""
              />
              <span className="absolute bottom-1 right-1 bg-black/85 text-[10px] text-white px-1 rounded font-mono">
                {formatDuration(clip.duration)}
              </span>
            </div>
            <div className="flex flex-col justify-center min-w-0 py-0.5">
              {clip.promo_label ? (
                <span className="text-[10px] font-bold uppercase tracking-wide text-amber-400/90 mb-0.5">
                  {clip.promo_label}
                </span>
              ) : null}
              <h4 className="text-sm font-semibold text-white line-clamp-2 leading-snug group-hover:text-brand-300 transition-colors">
                {clip.title}
              </h4>
              <div className="flex items-center gap-1.5 mt-1 min-w-0">
                <img
                  src={clip.broadcaster_avatar || `https://api.dicebear.com/7.x/avataaars/svg?seed=${clip.broadcaster_name}`}
                  className="w-4 h-4 rounded-full bg-brand-900 border border-brand-700/50 shrink-0"
                  alt=""
                />
                <span className="text-xs text-gray-400 truncate">{clip.broadcaster_name}</span>
              </div>
              <div className="text-[11px] text-gray-500 flex items-center gap-1 mt-0.5">
                <Eye className="w-3 h-3" /> {formatViews(clip.view_count)}
              </div>
            </div>
          </div>
        ))}
      </div>

      <div className="mt-4 pt-3 border-t border-brand-700/40 space-y-2">
        <p className="text-[10px] font-semibold uppercase tracking-wide text-gray-500">Дальше по сайту</p>
        <div className="flex flex-col gap-1.5">
          <Link
            to={feedStart}
            className="inline-flex items-center gap-2 text-xs font-medium text-brand-300 hover:text-white transition-colors py-1.5"
          >
            <Rows3 className="w-3.5 h-3.5 shrink-0" />
            Продолжить в вертикальной ленте
          </Link>
          <Link
            to="/trends"
            className="inline-flex items-center gap-2 text-xs font-medium text-amber-400/90 hover:text-amber-300 transition-colors py-1.5"
          >
            <Flame className="w-3.5 h-3.5 shrink-0" />
            Тренды
          </Link>
          {categoryGameId > 0 ? (
            <Link
              to={`/category/${categoryGameId}`}
              className="inline-flex items-center gap-2 text-xs font-medium text-gray-400 hover:text-white transition-colors py-1.5"
            >
              <Compass className="w-3.5 h-3.5 shrink-0" />
              Ещё в этой категории
            </Link>
          ) : (
            <Link
              to="/categories"
              className="inline-flex items-center gap-2 text-xs font-medium text-gray-400 hover:text-white transition-colors py-1.5"
            >
              <Compass className="w-3.5 h-3.5 shrink-0" />
              Все категории
            </Link>
          )}
        </div>
      </div>
    </div>
  );
}
