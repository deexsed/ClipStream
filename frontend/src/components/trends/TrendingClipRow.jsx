import { Eye } from 'lucide-react';

function formatViews(n) {
  if (n >= 1_000_000) return `${(n / 1_000_000).toFixed(1)}M`;
  if (n >= 1_000) return `${(n / 1_000).toFixed(1)}K`;
  return String(n ?? 0);
}

function formatDurationSec(sec) {
  const s = Math.max(0, Math.floor(Number(sec) || 0));
  const m = Math.floor(s / 60);
  const r = s % 60;
  return `${m}:${String(r).padStart(2, '0')}`;
}

function formatWhen(isoStr) {
  if (!isoStr) return '';
  try {
    const d = new Date(isoStr);
    if (isNaN(d)) return '';
    const now = new Date();
    const mins = Math.floor((now - d) / 60000);
    if (mins < 60) return `${mins} мин назад`;
    const h = Math.floor(mins / 60);
    if (h < 24) return `${h} ч назад`;
    const days = Math.floor(h / 24);
    if (days === 1) return 'вчера';
    if (days < 7) return `${days} дн назад`;
    if (days < 30) return `${Math.floor(days / 7)} нед назад`;
    if (days < 365) return `${Math.floor(days / 30)} мес назад`;
    return `${Math.floor(days / 365)} г назад`;
  } catch {
    return '';
  }
}

export default function TrendingClipRow({ clip, rank, isLive, onClick }) {
  const when = formatWhen(clip.twitch_created_at || clip.created_at);
  const lang = clip.language ? String(clip.language).toUpperCase() : null;

  return (
    <button
      type="button"
      onClick={() => onClick(clip)}
      className="group w-full flex gap-3 sm:gap-5 text-left rounded-xl sm:rounded-2xl p-2 sm:p-3 -mx-2 sm:-mx-3 hover:bg-white/[0.04] transition-colors border border-transparent hover:border-white/[0.06]"
    >
      <div
        className={`w-9 sm:w-14 shrink-0 flex items-start justify-end sm:justify-center pt-1 sm:pt-2 text-lg sm:text-2xl font-semibold tabular-nums ${
          rank <= 3 ? 'text-amber-400' : 'text-gray-600 group-hover:text-gray-500'
        }`}
      >
        {rank}
      </div>

      <div className="relative w-[140px] sm:w-[200px] md:w-[240px] shrink-0 aspect-video rounded-lg overflow-hidden bg-black ring-1 ring-white/10">
        <img
          src={clip.thumbnail_url}
          alt=""
          className="h-full w-full object-cover transition-transform duration-300 group-hover:scale-[1.02] opacity-95 group-hover:opacity-100"
          loading="lazy"
        />
        <span className="absolute bottom-1.5 right-1.5 rounded bg-black/85 px-1 py-0.5 text-[10px] font-bold text-white tabular-nums">
          {formatDurationSec(clip.duration)}
        </span>
        {isLive && (
          <span className="absolute top-1.5 left-1.5 rounded bg-red-600 px-1.5 py-0.5 text-[9px] font-bold uppercase tracking-wide text-white">
            Live
          </span>
        )}
      </div>

      <div className="min-w-0 flex-1 py-0.5 sm:py-1 pr-1">
        <h3 className="text-sm sm:text-base font-semibold text-white leading-snug line-clamp-2 group-hover:text-amber-100/95 transition-colors">
          {clip.title}
        </h3>
        <p className="mt-1.5 text-xs sm:text-sm text-gray-500 truncate">
          {clip.broadcaster_name}
          {clip.game_name ? <span className="text-gray-600"> · {clip.game_name}</span> : null}
        </p>
        <div className="mt-2 flex flex-wrap items-center gap-x-2 gap-y-1 text-[11px] sm:text-xs text-gray-600">
          <span className="inline-flex items-center gap-1">
            <Eye className="w-3.5 h-3.5 opacity-70" />
            {formatViews(clip.view_count)} просмотров
          </span>
          {when ? <span className="text-gray-600">· {when}</span> : null}
          {lang ? (
            <span className="rounded bg-white/[0.06] px-1.5 py-0.5 text-[10px] font-semibold text-gray-500">{lang}</span>
          ) : null}
          {clip.promo_label ? (
            <span className="rounded border border-amber-500/30 bg-amber-500/10 px-1.5 py-0.5 text-[10px] font-semibold text-amber-200/90">
              {clip.promo_label}
            </span>
          ) : null}
        </div>
      </div>
    </button>
  );
}
