import { useState, useEffect, useRef, useCallback } from 'react';
import { useNavigate } from 'react-router-dom';
import { Eye, RefreshCw } from 'lucide-react';
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
  const navigate = useNavigate();
  const clipLangKey = useClipLangStore((s) => s.codes.join(','));
  const mixRef = useRef(0);

  const load = useCallback(
    (newMix, excludeIds = null) => {
      if (!currentClipId) return;
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
      clipsApi
        .related(currentClipId, params.toString())
        .then((data) => {
          const raw = (data.clips || []).filter((c) => c.id !== currentClipId);
          setRecs(raw.slice(0, 10));
        })
        .catch(() => setRecs([]))
        .finally(() => setBusy(false));
    },
    [currentClipId],
  );

  useEffect(() => {
    mixRef.current = 0;
    load();
  }, [currentClipId, clipLangKey, load]);

  useEffect(() => {
    onQueueChange?.(recs);
  }, [recs, onQueueChange]);

  if (!currentClipId) return null;
  if (!busy && recs.length === 0) return null;

  return (
    <div className="rounded-2xl border border-brand-700/45 bg-gradient-to-b from-brand-900/70 to-brand-950/90 p-4 shadow-xl shadow-black/20">
      <div className="flex items-center justify-between gap-2 mb-4">
        <h3 className="font-bold text-white text-sm tracking-tight">Похожее</h3>
        <button
          type="button"
          disabled={busy}
          onClick={() => load(Math.floor(Math.random() * 1_000_000_000), recs.map((c) => c.id))}
          className="inline-flex items-center gap-1.5 text-[11px] text-brand-300 hover:text-white px-2 py-1 rounded-lg border border-brand-700/60 bg-brand-950/50 disabled:opacity-40"
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
    </div>
  );
}
