import { useEffect, useMemo, useState } from 'react';
import { Link, useNavigate, useParams } from 'react-router-dom';
import {
  ArrowLeft,
  ChevronRight,
  Search,
  Flame,
  Clock,
  Timer,
  Users,
  ExternalLink,
  Eye,
  Film,
  Calendar,
  Radio,
  ChevronDown,
} from 'lucide-react';
import { clips as clipsApi, streams as streamsApi, channels as channelsApi } from '../api/client';
import { useClipLangStore } from '../store/useClipLangStore';
import ClipGrid from '../components/clips/ClipGrid';

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

function formatCompact(n) {
  if (n == null || Number.isNaN(Number(n))) return '—';
  const x = Number(n);
  if (x >= 1_000_000) return `${(x / 1_000_000).toFixed(1).replace(/\.0$/, '')} млн`;
  if (x >= 1_000) return `${(x / 1_000).toFixed(1).replace(/\.0$/, '')} тыс.`;
  return String(x);
}

function twitchSinceRu(iso) {
  if (!iso) return null;
  try {
    const d = new Date(iso);
    return d.toLocaleDateString('ru-RU', { year: 'numeric', month: 'long', day: 'numeric' });
  } catch {
    return null;
  }
}

export default function ChannelPage() {
  const { login } = useParams();
  const channelLogin = decodeURIComponent(login || '');
  const navigate = useNavigate();

  const [clips, setClips] = useState([]);
  const [sort, setSort] = useState('popular');
  const [period, setPeriod] = useState('all');
  const [search, setSearch] = useState('');
  const [input, setInput] = useState('');
  const [offset, setOffset] = useState(0);
  const [hasMore, setHasMore] = useState(true);
  const [loading, setLoading] = useState(false);
  const [isLive, setIsLive] = useState(false);
  const [similarChannels, setSimilarChannels] = useState([]);
  const [profile, setProfile] = useState(null);
  const [profileLoading, setProfileLoading] = useState(true);
  const [descExpanded, setDescExpanded] = useState(false);
  const clipLangKey = useClipLangStore((s) => s.codes.join(','));

  useEffect(() => {
    if (!channelLogin) return;
    setProfileLoading(true);
    channelsApi
      .profile(channelLogin)
      .then(setProfile)
      .catch(() => setProfile({ found: false }))
      .finally(() => setProfileLoading(false));
  }, [channelLogin]);

  useEffect(() => {
    if (!channelLogin) return;
    streamsApi.isLive([channelLogin]).then((d) => setIsLive(!!d?.live?.[channelLogin])).catch(() => {});
  }, [channelLogin]);

  useEffect(() => {
    channelsApi.similar(6).then((d) => setSimilarChannels(d.channels || [])).catch(() => setSimilarChannels([]));
  }, []);

  useEffect(() => {
    if (!channelLogin) return;
    setClips([]);
    setOffset(0);
    setHasMore(true);
    load(0, true);
  }, [channelLogin, sort, period, search, clipLangKey]);

  const tw = profile?.twitch;
  const effectiveLive = tw?.is_live !== undefined && tw?.is_live !== null ? tw.is_live : isLive;
  const liveMap = useMemo(
    () => ({ [channelLogin]: !!effectiveLive }),
    [channelLogin, effectiveLive],
  );
  const displayName = tw?.display_name || channelLogin;
  const resolvedLogin = tw?.login || channelLogin;
  const avatarUrl =
    tw?.profile_image_url ||
    `https://api.dicebear.com/7.x/avataaars/svg?seed=${encodeURIComponent(channelLogin)}`;
  const bannerUrl = tw?.is_live && tw?.stream?.thumbnail_url
    ? tw.stream.thumbnail_url
    : tw?.offline_image_url || '';
  const description = (tw?.description || '').trim();
  const partnerBadge = tw?.broadcaster_type === 'partner';
  const affiliateBadge = tw?.broadcaster_type === 'affiliate';
  const twitchUrl = `https://www.twitch.tv/${resolvedLogin}`;

  const load = (off, replace = false) => {
    setLoading(true);
    const params = new URLSearchParams({
      channel_login: channelLogin,
      sort,
      period,
      limit: '20',
      offset: String(off),
    });
    if (search.trim()) params.set('search', search.trim());
    clipsApi
      .feed(params.toString())
      .then((data) => {
        const incoming = data.clips || [];
        setClips((prev) => (replace ? incoming : [...prev, ...incoming]));
        setOffset(off + incoming.length);
        setHasMore(incoming.length >= 20);
      })
      .catch(() => {})
      .finally(() => setLoading(false));
  };

  const onMore = () => {
    if (!hasMore || loading) return;
    load(offset, false);
  };

  return (
    <div className="min-h-screen pb-16">
      {/* Hero */}
      <div className="relative">
        <div
          className="h-[min(320px,42vw)] sm:h-[280px] w-full bg-gradient-to-br from-brand-950 via-brand-900 to-[#1a0a2e] relative overflow-hidden"
          style={
            bannerUrl
              ? {
                  backgroundImage: `linear-gradient(180deg, rgba(14,14,16,0.55) 0%, rgba(14,14,16,0.92) 100%), url(${bannerUrl})`,
                  backgroundSize: 'cover',
                  backgroundPosition: 'center top',
                }
              : undefined
          }
        >
          <div className="absolute inset-0 bg-gradient-to-t from-brand-900 via-brand-900/50 to-transparent pointer-events-none" />
        </div>

        <div className="max-w-[1700px] mx-auto px-4 sm:px-6 lg:px-8 -mt-24 sm:-mt-28 relative z-10">
          <div className="flex items-center gap-2 text-sm text-gray-400 mb-4">
            <Link to="/" className="hover:text-white transition-colors">
              Главная
            </Link>
            <ChevronRight className="w-3 h-3 shrink-0" />
            <span className="text-white font-medium truncate">{displayName}</span>
          </div>

          <div className="rounded-2xl border border-brand-700/60 bg-brand-900/85 backdrop-blur-md shadow-2xl shadow-black/40 p-5 sm:p-6">
            <div className="flex flex-col lg:flex-row gap-6 lg:gap-10">
              <div className="flex items-start gap-4 sm:gap-5 shrink-0">
                <div className="relative">
                  <img
                    src={avatarUrl}
                    alt=""
                    className={`w-24 h-24 sm:w-28 sm:h-28 rounded-2xl object-cover ring-4 shadow-xl ${
                      tw?.is_live || isLive ? 'ring-accent-red ring-offset-2 ring-offset-brand-900' : 'ring-brand-700'
                    }`}
                  />
                  {(tw?.is_live || isLive) && (
                    <span className="absolute -bottom-1 -right-1 flex items-center gap-1 text-[10px] font-bold uppercase tracking-wide px-2 py-0.5 rounded-md bg-accent-red text-white shadow-lg">
                      <Radio className="w-3 h-3" /> Live
                    </span>
                  )}
                </div>
                <div className="min-w-0 flex-1 pt-1">
                  <div className="flex flex-wrap items-center gap-2 gap-y-1">
                    <h1 className="text-2xl sm:text-3xl font-black text-white tracking-tight truncate">{displayName}</h1>
                    {partnerBadge && (
                      <span className="text-[11px] font-bold px-2 py-0.5 rounded-md bg-[#5c16c5] text-white">Partner</span>
                    )}
                    {affiliateBadge && !partnerBadge && (
                      <span className="text-[11px] font-bold px-2 py-0.5 rounded-md bg-sky-700/80 text-sky-100">Affiliate</span>
                    )}
                  </div>
                  <p className="text-brand-400 font-medium mt-0.5">@{resolvedLogin}</p>

                  <div className="flex flex-wrap gap-3 sm:gap-4 mt-4 text-sm text-gray-300">
                    <span className="inline-flex items-center gap-1.5" title="Просмотры канала на Twitch">
                      <Eye className="w-4 h-4 text-brand-400 shrink-0" />
                      <span className="text-gray-400">Twitch:</span>
                      <span className="text-white font-semibold tabular-nums">
                        {profileLoading ? '…' : formatCompact(tw?.view_count)}
                      </span>
                    </span>
                    <span className="inline-flex items-center gap-1.5" title="Клипов в ClipStream">
                      <Film className="w-4 h-4 text-emerald-400 shrink-0" />
                      <span className="text-gray-400">Здесь:</span>
                      <span className="text-white font-semibold tabular-nums">
                        {profileLoading ? '…' : formatCompact(profile?.clipstream_clips ?? 0)}
                      </span>
                    </span>
                    {tw?.created_at && (
                      <span className="inline-flex items-center gap-1.5 text-gray-400">
                        <Calendar className="w-4 h-4 shrink-0" />
                        <span className="hidden sm:inline">На Twitch с</span>
                        <span className="text-gray-200">{twitchSinceRu(tw.created_at)}</span>
                      </span>
                    )}
                  </div>

                  {!profileLoading && profile?.found === false && (
                    <p className="mt-3 text-xs text-amber-400/90">
                      Профиль на Twitch не найден — показываем клипы по имени в ссылке.
                    </p>
                  )}

                  {description && (
                    <div className="mt-4">
                      <p
                        className={`text-sm text-gray-300 leading-relaxed whitespace-pre-wrap ${
                          !descExpanded ? 'line-clamp-3' : ''
                        }`}
                      >
                        {description}
                      </p>
                      {description.length > 140 && (
                        <button
                          type="button"
                          onClick={() => setDescExpanded(!descExpanded)}
                          className="mt-1 inline-flex items-center gap-1 text-xs font-medium text-brand-400 hover:text-brand-300"
                        >
                          {descExpanded ? 'Свернуть' : 'Показать полностью'}
                          <ChevronDown className={`w-3.5 h-3.5 transition-transform ${descExpanded ? 'rotate-180' : ''}`} />
                        </button>
                      )}
                    </div>
                  )}

                  <div className="flex flex-wrap gap-2 mt-5">
                    <a
                      href={twitchUrl}
                      target="_blank"
                      rel="noopener noreferrer"
                      className="inline-flex items-center gap-2 px-4 py-2 rounded-xl bg-brand-500 hover:bg-brand-600 text-white text-sm font-semibold shadow-lg shadow-brand-900/50 transition-colors"
                    >
                      <ExternalLink className="w-4 h-4" />
                      Открыть на Twitch
                    </a>
                    <button
                      type="button"
                      onClick={() => navigate(-1)}
                      className="inline-flex items-center gap-2 px-4 py-2 rounded-xl bg-brand-800 border border-brand-700 text-gray-200 text-sm hover:bg-brand-700 hover:text-white transition-colors"
                    >
                      <ArrowLeft className="w-4 h-4" />
                      Назад
                    </button>
                  </div>
                </div>
              </div>
            </div>

            {/* Live card */}
            {tw?.is_live && tw?.stream && (
              <a
                href={twitchUrl}
                target="_blank"
                rel="noopener noreferrer"
                className="mt-6 block rounded-xl border border-white/10 overflow-hidden bg-black/30 hover:border-accent-red/40 transition-colors group"
              >
                <div className="grid sm:grid-cols-[minmax(0,280px)_1fr] gap-0">
                  <div className="aspect-video sm:aspect-auto sm:min-h-[140px] relative bg-black">
                    <img
                      src={tw.stream.thumbnail_url}
                      alt=""
                      className="absolute inset-0 w-full h-full object-cover opacity-90 group-hover:opacity-100 transition-opacity"
                    />
                    <div className="absolute inset-0 bg-gradient-to-t from-black/80 to-transparent sm:bg-gradient-to-r from-transparent to-black/50" />
                  </div>
                  <div className="p-4 sm:p-5 flex flex-col justify-center min-w-0">
                    <div className="flex items-center gap-2 text-accent-red text-xs font-bold uppercase tracking-wider mb-1">
                      <Radio className="w-4 h-4" /> Сейчас в эфире
                    </div>
                    <h3 className="text-lg font-bold text-white line-clamp-2 group-hover:text-brand-200 transition-colors">
                      {tw.stream.title || '—'}
                    </h3>
                    <div className="flex flex-wrap gap-3 mt-2 text-sm text-gray-400">
                      {tw.stream.game_name && (
                        <span className="text-emerald-300/90">{tw.stream.game_name}</span>
                      )}
                      <span className="text-white font-semibold tabular-nums">
                        {formatCompact(tw.stream.viewer_count)} зрителей
                      </span>
                    </div>
                  </div>
                </div>
              </a>
            )}
          </div>
        </div>
      </div>

      <div className="max-w-[1700px] mx-auto px-4 sm:px-6 lg:px-8 pt-8">
        <div className="rounded-2xl border border-brand-700/50 bg-brand-900/40 p-4 mb-8">
          <p className="text-xs text-gray-500 uppercase tracking-wider mb-3">Клипы канала</p>
          <div className="flex flex-col xl:flex-row xl:items-end gap-3 xl:gap-4">
            <div className="flex flex-wrap gap-2">
              {SORTS.map(({ key, label, icon: Icon }) => (
                <button
                  key={key}
                  type="button"
                  onClick={() => setSort(key)}
                  className={`px-3 py-2 rounded-lg text-sm border transition-colors ${
                    sort === key
                      ? 'bg-brand-500 border-brand-500 text-white shadow-md shadow-brand-900/30'
                      : 'bg-brand-800/80 border-brand-700 text-gray-300 hover:text-white'
                  }`}
                >
                  <Icon className="w-3.5 h-3.5 inline mr-1.5 -mt-0.5" />
                  {label}
                </button>
              ))}
            </div>
            <div className="flex flex-wrap gap-2 items-center flex-1">
              <select
                value={period}
                onChange={(e) => setPeriod(e.target.value)}
                className="px-3 py-2 rounded-lg text-sm bg-brand-800 border border-brand-700 text-gray-200 min-w-[140px]"
              >
                {PERIODS.map((p) => (
                  <option key={p.key} value={p.key}>
                    {p.label}
                  </option>
                ))}
              </select>
              <div className="relative flex-1 min-w-[200px] max-w-xl">
                <input
                  value={input}
                  onChange={(e) => setInput(e.target.value)}
                  onKeyDown={(e) => {
                    if (e.key === 'Enter') setSearch(input);
                  }}
                  placeholder="Поиск по заголовку клипа"
                  className="w-full pl-9 pr-3 py-2 rounded-lg text-sm bg-brand-800 border border-brand-700 text-gray-200 placeholder:text-gray-500"
                />
                <Search className="w-4 h-4 text-gray-500 absolute left-3 top-2.5" />
              </div>
              <button
                type="button"
                onClick={() => setSearch(input)}
                className="px-4 py-2 rounded-lg text-sm font-medium bg-brand-600 text-white hover:bg-brand-500"
              >
                Искать
              </button>
            </div>
          </div>
        </div>

        {clips.length === 0 && !loading ? (
          <div className="py-16 text-center text-gray-500 rounded-2xl border border-dashed border-brand-700/50">
            По выбранным параметрам клипы не найдены.
          </div>
        ) : (
          <>
            <ClipGrid clips={clips} liveMap={liveMap} onClipClick={(clip) => navigate(`/clip/${clip.id}`)} />
            {hasMore && (
              <div className="flex justify-center mt-10">
                <button
                  type="button"
                  onClick={onMore}
                  disabled={loading}
                  className="px-8 py-2.5 rounded-xl bg-brand-800 border border-brand-700 text-white hover:bg-brand-700 disabled:opacity-60 font-medium"
                >
                  {loading ? 'Загрузка...' : 'Показать ещё'}
                </button>
              </div>
            )}
          </>
        )}

        {similarChannels.length > 0 && (
          <section className="mt-14 border-t border-brand-800/80 pt-10">
            <h2 className="text-xl font-bold text-white mb-4 flex items-center gap-2">
              <Users className="w-5 h-5 text-emerald-400" /> Похожие стримеры
            </h2>
            <div className="grid grid-cols-1 md:grid-cols-2 xl:grid-cols-3 gap-3">
              {similarChannels.map((ch) => (
                <button
                  key={ch.broadcaster_id}
                  type="button"
                  onClick={() => navigate(`/channel/${encodeURIComponent(ch.broadcaster_name || ch.broadcaster_id)}`)}
                  className="text-left p-4 rounded-xl border border-brand-700/60 bg-brand-800/30 hover:bg-brand-800/60 hover:border-brand-600 transition-colors"
                >
                  <div className="text-white font-semibold truncate">{ch.broadcaster_name || ch.broadcaster_id}</div>
                  <div className="text-xs text-gray-400 truncate">{ch.game_name || 'Разные категории'}</div>
                  <div className="text-xs text-gray-500 mt-1">{ch.clips_count || 0} клипов</div>
                </button>
              ))}
            </div>
          </section>
        )}
      </div>
    </div>
  );
}
