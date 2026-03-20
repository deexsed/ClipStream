import { Eye, Radio } from 'lucide-react';

function formatViewers(n) {
  if (!n && n !== 0) return '0';
  if (n >= 1000000) return `${(n / 1000000).toFixed(1)}M`;
  if (n >= 1000) return `${(n / 1000).toFixed(1)}K`;
  return String(n);
}

export default function LiveStreamCard({ stream }) {
  const channelUrl = `https://www.twitch.tv/${stream.user_login || stream.user_name}`;

  return (
    <a
      href={channelUrl}
      target="_blank"
      rel="noopener noreferrer"
      className="group block bg-brand-800/60 rounded-xl overflow-hidden border border-brand-700/30 hover:border-brand-500/40 hover:shadow-xl hover:shadow-brand-900/40 transition-all duration-300"
    >
      <div className="relative aspect-video overflow-hidden bg-black">
        <img
          src={stream.thumbnail_url}
          alt={stream.title}
          loading="lazy"
          className="w-full h-full object-cover opacity-90 group-hover:scale-105 transition-transform duration-500"
        />
        <div className="absolute top-2 left-2 inline-flex items-center gap-1 bg-red-600/90 text-white px-2 py-0.5 rounded text-[10px] font-bold">
          <Radio className="w-3 h-3" />
          LIVE
        </div>
      </div>

      <div className="p-3">
        <div className="flex items-center gap-2 mb-2">
          <img
            src={stream.avatar_url || `https://api.dicebear.com/7.x/avataaars/svg?seed=${stream.user_name || 'stream'}`}
            className="w-6 h-6 rounded-full border border-brand-700/50 bg-brand-900 shrink-0"
            alt=""
          />
          <div className="min-w-0">
            <div className="text-xs text-white font-semibold truncate">{stream.user_name}</div>
            <div className="text-[11px] text-brand-400 truncate">{stream.game_name}</div>
          </div>
        </div>
        <div className="text-sm text-white font-semibold line-clamp-2 leading-snug mb-2">{stream.title}</div>
        <div className="flex items-center gap-1 text-xs text-gray-400">
          <Eye className="w-3 h-3" />
          {formatViewers(stream.viewer_count)} зрителей
        </div>
      </div>
    </a>
  );
}

