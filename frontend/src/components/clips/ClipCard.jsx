import { useMemo } from 'react';
import { Play, Eye } from 'lucide-react';

function formatDuration(seconds) {
  const m = Math.floor(seconds / 60);
  const s = Math.floor(seconds % 60);
  return `${m}:${String(s).padStart(2, '0')}`;
}

function formatViews(count) {
  if (count >= 1000000) return `${(count / 1000000).toFixed(1)}M`;
  if (count >= 1000) return `${(count / 1000).toFixed(1)}K`;
  return String(count);
}

function formatDate(isoStr) {
  if (!isoStr) return '';
  try {
    const d = new Date(isoStr);
    if (isNaN(d)) return '';
    const now = new Date();
    const days = Math.floor((now - d) / 86400000);
    if (days === 0) return 'сегодня';
    if (days === 1) return 'вчера';
    if (days < 7) return `${days} дн.`;
    if (days < 30) return `${Math.floor(days / 7)} нед.`;
    if (days < 365) return `${Math.floor(days / 30)} мес.`;
    return `${Math.floor(days / 365)} г.`;
  } catch { return ''; }
}

function escapeRegExp(str) {
  return str.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

function renderHighlighted(text, query) {
  if (!query || !text) return text;
  const terms = query
    .toLowerCase()
    .split(/\s+/)
    .map((s) => s.trim())
    .filter((s) => s.length >= 2);
  if (!terms.length) return text;
  const re = new RegExp(`(${terms.map((t) => escapeRegExp(t)).join('|')})`, 'ig');
  const parts = String(text).split(re);
  return parts.map((part, idx) => (
    terms.includes(part.toLowerCase())
      ? <mark key={idx} className="bg-brand-500/35 text-white rounded px-0.5">{part}</mark>
      : <span key={idx}>{part}</span>
  ));
}

const SHELF_LABELS = {
  for_you: 'Для вас',
  fresh: 'Новинки',
  explore: 'Открытие',
};

export default function ClipCard({
  clip,
  onClick,
  index = 0,
  isLive = false,
  highlight = '',
  shelfKey = null,
  trendRank = null,
  trendBadgeLine = '',
  trendPill = '',
}) {
  const dateStr = formatDate(clip.twitch_created_at || clip.created_at);
  const thumb = useMemo(() => clip.thumbnail_url, [clip.thumbnail_url]);
  const shelfLabel =
    trendRank == null && shelfKey && SHELF_LABELS[shelfKey] ? SHELF_LABELS[shelfKey] : null;
  const trendCard = trendRank != null;

  return (
    <div
      onClick={() => onClick(clip)}
      className={`group clip-card rounded-2xl overflow-hidden cursor-pointer animate-fade-in transition-all duration-300 ${
        trendCard
          ? 'bg-brand-900/25 border border-white/[0.06] hover:border-white/[0.12] hover:bg-brand-900/40 shadow-none hover:shadow-lg hover:shadow-black/20'
          : 'bg-brand-900/50 border border-brand-700/40 hover:border-amber-500/35 hover:shadow-xl hover:shadow-black/40 ring-1 ring-black/20'
      }`}
      style={{ animationDelay: `${index * 0.04}s` }}
    >
      <div className="relative aspect-video overflow-hidden bg-black">
        <img
          src={thumb}
          alt={clip.title}
          loading="lazy"
          className={`clip-thumbnail w-full h-full object-cover transition-transform duration-500 group-hover:scale-[1.03] ${
            clip.viewed ? 'opacity-75 group-hover:opacity-95' : 'opacity-90 group-hover:opacity-100'
          }`}
        />
        <div className="absolute inset-0 bg-gradient-to-t from-black/70 via-transparent to-transparent opacity-60" />

        <div className="absolute bottom-2 right-2 bg-black/80 backdrop-blur-sm px-1.5 py-0.5 rounded text-[11px] font-bold text-white">
          {formatDuration(clip.duration)}
        </div>

        {trendRank != null && (
          <div className="absolute top-2 left-2 flex flex-col items-start gap-1 max-w-[min(100%,12rem)] z-[1]">
            <div className="bg-gradient-to-br from-amber-400 to-orange-500 text-black rounded-lg shadow-lg shadow-black/50 px-2.5 py-1.5 leading-none">
              <span className="font-black text-lg tabular-nums tracking-tight">#{trendRank}</span>
              {(trendBadgeLine || 'В ТРЕНДЕ') && (
                <span className="block text-[10px] font-black tracking-wide mt-0.5 opacity-95 leading-tight">
                  {trendBadgeLine || 'В ТРЕНДЕ'}
                </span>
              )}
            </div>
            {trendPill ? (
              <span className="text-[10px] font-bold text-white bg-black/75 backdrop-blur-sm px-2 py-0.5 rounded-md border border-white/15">
                {trendPill}
              </span>
            ) : null}
          </div>
        )}

        {shelfLabel && (
          <div className="absolute top-2 left-2 bg-black/75 backdrop-blur-sm px-2 py-0.5 rounded text-[10px] font-bold text-white/95 tracking-wide">
            {shelfLabel}
          </div>
        )}

        {isLive && (
          <div className="absolute top-2 right-2 flex items-center gap-1.5 bg-red-600/90 backdrop-blur-sm px-2 py-0.5 rounded-full">
            <span className="relative flex h-2 w-2">
              <span className="animate-ping absolute inline-flex h-full w-full rounded-full bg-red-300 opacity-75" />
              <span className="relative inline-flex rounded-full h-2 w-2 bg-white" />
            </span>
            <span className="text-[10px] font-bold text-white uppercase tracking-wider">Live</span>
          </div>
        )}

        <div className="absolute inset-0 flex items-center justify-center opacity-0 group-hover:opacity-100 transition-opacity duration-300">
          <div className="w-11 h-11 bg-brand-500/90 rounded-full flex items-center justify-center text-white shadow-lg transform scale-75 group-hover:scale-100 transition-transform">
            <Play className="w-4 h-4 fill-current ml-0.5" />
          </div>
        </div>
      </div>

      <div className="p-3">
        <h3 className="text-white font-semibold text-sm mb-1.5 line-clamp-2 leading-snug group-hover:text-brand-400 transition-colors">
          {renderHighlighted(clip.title, highlight)}
        </h3>

        <div className="flex flex-wrap gap-1.5 mb-1.5">
          {clip.viewed && (
            <span className="inline-block text-[11px] text-gray-400 bg-gray-900/60 border border-gray-600/40 px-1.5 py-0.5 rounded">
              Просмотрено
            </span>
          )}
          {clip.game_name && (
            <span className="inline-block text-[11px] text-brand-400/80 bg-brand-900/50 border border-brand-700/40 px-1.5 py-0.5 rounded">
              {clip.game_name}
            </span>
          )}
          {clip.language && (
            <span className="inline-block text-[11px] text-gray-400 bg-brand-900/40 border border-brand-700/30 px-1.5 py-0.5 rounded uppercase">
              {clip.language}
            </span>
          )}
        </div>

        <div className="flex items-center justify-between mt-1.5">
          <div className="flex items-center gap-1.5 min-w-0">
            <img
              src={clip.broadcaster_avatar || `https://api.dicebear.com/7.x/avataaars/svg?seed=${clip.broadcaster_name}`}
              className="w-5 h-5 rounded-full bg-brand-900 border border-brand-700/50 shrink-0" alt=""
            />
            <span className="text-xs text-gray-400 truncate">{renderHighlighted(clip.broadcaster_name, highlight)}</span>
            {isLive && (
              <span className="w-2 h-2 rounded-full bg-red-500 shrink-0" title="В эфире" />
            )}
          </div>
          <div className="flex items-center gap-2 text-[11px] text-gray-500 shrink-0">
            <span className="flex items-center gap-0.5"><Eye className="w-3 h-3" />{formatViews(clip.view_count)}</span>
            {dateStr && <span>{dateStr}</span>}
          </div>
        </div>
      </div>
    </div>
  );
}
