import { useCallback, useEffect, useState } from 'react';
import { useNavigate, useSearchParams } from 'react-router-dom';
import { clips as clipsApi } from '../api/client';
import { useClipLangStore } from '../store/useClipLangStore';
import { TWITCH_BROADCAST_LANGUAGES } from '../data/twitchLanguages';
import useLiveStatus from '../hooks/useLiveStatus';
import TrendingClipRow from '../components/trends/TrendingClipRow';
import ClipLanguageInlineBar from '../components/clips/ClipLanguageInlineBar';
import { TREND_PERIODS, TREND_TOP_LIMIT, buildTrendClipSearch } from '../constants/trendsUi';

const ALLOWED_LANGS = new Set(TWITCH_BROADCAST_LANGUAGES.map((l) => l.code));

export default function TrendsPage() {
  const navigate = useNavigate();
  const [searchParams, setSearchParams] = useSearchParams();
  const clipLangKey = useClipLangStore((s) => s.codes.join(','));

  const [periodKey, setPeriodKey] = useState(() => {
    const t = searchParams.get('t');
    if (t && TREND_PERIODS.some((p) => p.key === t)) return t;
    return 'today';
  });

  const [clips, setClips] = useState([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState('');

  const periodMeta = TREND_PERIODS.find((p) => p.key === periodKey) || TREND_PERIODS[0];

  /** При открытии с ?langs= — применить к глобальному фильтру (как в шапке). */
  useEffect(() => {
    const raw = searchParams.get('langs');
    if (raw === null) return;
    const setCodes = useClipLangStore.getState().setCodes;
    const trimmed = String(raw).trim();
    if (!trimmed) {
      setCodes([]);
      return;
    }
    const next = trimmed
      .split(',')
      .map((s) => s.trim().toLowerCase())
      .filter((c) => ALLOWED_LANGS.has(c));
    setCodes(next);
  }, [searchParams]);

  useEffect(() => {
    const t = searchParams.get('t');
    if (t && TREND_PERIODS.some((p) => p.key === t) && t !== periodKey) setPeriodKey(t);
  }, [searchParams, periodKey]);

  useEffect(() => {
    const t = searchParams.get('t');
    const tOk = TREND_PERIODS.some((p) => p.key === t);
    if (tOk) return;
    setSearchParams(
      (prev) => {
        const n = new URLSearchParams(prev);
        n.set('t', periodKey);
        return n;
      },
      { replace: true },
    );
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  const selectPeriod = (key) => {
    setPeriodKey(key);
    setSearchParams(
      (prev) => {
        const n = new URLSearchParams(prev);
        n.set('t', key);
        return n;
      },
      { replace: true },
    );
  };

  const load = useCallback(async () => {
    setLoading(true);
    setError('');
    try {
      const params = new URLSearchParams({
        sort: 'popular',
        period: periodMeta.period,
        limit: String(TREND_TOP_LIMIT),
        offset: '0',
      }).toString();
      const data = await clipsApi.feed(params);
      setClips(data.clips || []);
    } catch {
      setError('Не удалось загрузить список. Попробуйте ещё раз.');
      setClips([]);
    } finally {
      setLoading(false);
    }
  }, [periodMeta.period, clipLangKey]);

  useEffect(() => {
    void load();
  }, [load]);

  const liveMap = useLiveStatus(clips);

  const openClip = (clip, index) => {
    navigate(`/clip/${clip.id}?${buildTrendClipSearch(periodKey, index + 1)}`);
  };

  const langHint =
    clipLangKey === '' ? 'Все языки' : clipLangKey.split(',').map((c) => c.toUpperCase()).join(', ');

  return (
    <div className="min-h-screen bg-[#0d0d12] text-white">
      <div className="max-w-4xl mx-auto px-4 sm:px-6 lg:px-8 pt-8 sm:pt-10 pb-20">
        <header className="mb-8">
          <h1 className="text-[1.65rem] sm:text-3xl font-semibold tracking-tight">В тренде</h1>
          <p className="mt-2 text-sm text-gray-500 leading-relaxed">{periodMeta.subtitle}</p>
        </header>

        <div
          role="tablist"
          aria-label="Период"
          className="flex gap-1 p-1 mb-8 rounded-xl bg-white/[0.04] border border-white/[0.06]"
        >
          {TREND_PERIODS.map((p) => (
            <button
              key={p.key}
              type="button"
              role="tab"
              aria-selected={periodKey === p.key}
              onClick={() => selectPeriod(p.key)}
              className={`flex-1 min-w-0 py-2.5 px-2 sm:px-3 rounded-lg text-xs sm:text-sm font-medium transition-colors ${
                periodKey === p.key ? 'bg-white text-black shadow-sm' : 'text-gray-400 hover:text-gray-200'
              }`}
            >
              {p.tabLabel}
            </button>
          ))}
        </div>

        <div className="mb-8">
          <p className="text-[11px] font-medium uppercase tracking-widest text-gray-600 mb-2">
            Язык клипа
          </p>
          <p className="text-xs text-gray-500 mb-3">
            Тот же фильтр, что «Язык клипов» в шапке. Без галочек — все языки.
          </p>
          <ClipLanguageInlineBar syncUrl />
        </div>

        <div className="mb-4 flex items-baseline justify-between gap-3 border-b border-white/[0.08] pb-3">
          <h2 className="text-base font-medium text-gray-300">{periodMeta.pageTitle}</h2>
          <span className="text-xs text-gray-600 shrink-0 max-w-[55%] text-right truncate" title={langHint}>
            {langHint}
          </span>
        </div>

        {error ? (
          <p className="text-sm text-red-400/90 py-8" role="alert">
            {error}
          </p>
        ) : null}

        {loading ? (
          <div className="space-y-4 animate-pulse pt-2">
            {[1, 2, 3, 4, 5, 6].map((i) => (
              <div key={i} className="flex gap-4">
                <div className="w-8 h-8 rounded bg-white/[0.06]" />
                <div className="w-40 sm:w-60 aspect-video rounded-lg bg-white/[0.06]" />
                <div className="flex-1 space-y-2 py-1">
                  <div className="h-4 bg-white/[0.06] rounded w-3/4" />
                  <div className="h-3 bg-white/[0.04] rounded w-1/3" />
                </div>
              </div>
            ))}
          </div>
        ) : clips.length === 0 ? (
          <p className="text-sm text-gray-600 py-16 text-center">Нет клипов для этого периода и фильтра.</p>
        ) : (
          <ol className="space-y-1 sm:space-y-0 divide-y divide-white/[0.05] list-none p-0 m-0">
            {clips.map((clip, i) => (
              <li key={clip.id} className="py-2 sm:py-1">
                <TrendingClipRow
                  clip={clip}
                  rank={i + 1}
                  isLive={!!liveMap[clip.broadcaster_name]}
                  onClick={(c) => openClip(c, i)}
                />
              </li>
            ))}
          </ol>
        )}
      </div>
    </div>
  );
}
