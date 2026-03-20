import { useState, useEffect, useRef, useCallback, useMemo } from 'react';
import { Link } from 'react-router-dom';
import {
  ThumbsUp,
  ThumbsDown,
  Share2,
  ExternalLink,
  ChevronRight,
  Eye,
  Play,
  Calendar,
  Clock,
  Languages,
  Ban,
  Flag,
  Clapperboard,
  Sparkles,
} from 'lucide-react';
import { clips as clipsApi, streams as streamsApi } from '../../api/client';
import useAuthStore from '../../store/useAuthStore';
import CommentSection from './CommentSection';
import { formatViews, formatDurationSec, formatDateRelative } from '../../utils/clipFormatters';

/**
 * Основная колонка просмотра клипа: плеер (без оверлея поверх iframe), мета, лайки, комментарии.
 * Те же API и разметка, что на /clip/:id — для ленты и страницы клипа.
 */
export default function ClipViewerPanel({
  clipId,
  isActive = true,
  /** onMount — как раньше на ClipPage; whenActive — просмотр только у видимого слайда ленты */
  recordViewMode = 'onMount',
  showPlayNextToggle = true,
  showBreadcrumbs = false,
  nextClipId = null,
  onPlayNextNavigate,
  shareUrl,
  onFetchError,
  className = '',
  /** В ленте плеер только у активного слайда (остальные — постер без iframe) */
  embedOnlyWhenActive = false,
  /** Компактные отступы (лента) */
  compact = false,
}) {
  const { isAuth } = useAuthStore();

  const [clip, setClip] = useState(null);
  const [loading, setLoading] = useState(true);
  const [liked, setLiked] = useState(null);
  const [likeCount, setLikeCount] = useState(0);
  const [dislikeCount, setDislikeCount] = useState(0);
  const [copied, setCopied] = useState(false);
  const [copiedField, setCopiedField] = useState('');
  const [isLive, setIsLive] = useState(false);
  const [feedbackMsg, setFeedbackMsg] = useState('');
  const [playNext, setPlayNext] = useState(() => {
    try {
      return localStorage.getItem('clipstream_autoplay_next') === '1';
    } catch {
      return false;
    }
  });
  const pageOpenAt = useRef(null);
  const analyticsSessionIdRef = useRef(null);
  const playNextRef = useRef(playNext);

  playNextRef.current = playNext;

  useEffect(() => {
    setLoading(true);
    setLiked(null);
    clipsApi
      .get(clipId)
      .then((data) => {
        setClip(data);
        setLikeCount(data.like_count ?? 0);
        setDislikeCount(data.dislike_count ?? 0);
        const v = data.user_vote;
        if (v === 'like') setLiked(true);
        else if (v === 'dislike') setLiked(false);
        else setLiked(null);
      })
      .catch(() => {
        onFetchError?.();
      })
      .finally(() => setLoading(false));
  }, [clipId, onFetchError]);

  useEffect(() => {
    if (recordViewMode !== 'onMount') return;
    clipsApi.view(clipId).catch(() => {});
  }, [clipId, recordViewMode]);

  useEffect(() => {
    if (recordViewMode !== 'whenActive' || !isActive) return;
    clipsApi.view(clipId).catch(() => {});
  }, [clipId, recordViewMode, isActive]);

  useEffect(() => {
    try {
      localStorage.setItem('clipstream_autoplay_next', playNext ? '1' : '0');
    } catch {}
  }, [playNext]);

  useEffect(() => {
    if (!showPlayNextToggle || !playNext || !clip?.id || !onPlayNextNavigate) return;
    const dur = Math.max(Number(clip.duration) || 1, 1);
    const ms = (dur + 3) * 1000;
    const curId = Number(clip.id);
    const nextId = nextClipId != null ? Number(nextClipId) : null;
    if (nextId == null || Number.isNaN(nextId) || nextId === curId) return;
    const t = window.setTimeout(() => {
      if (!playNextRef.current) return;
      onPlayNextNavigate(nextId);
    }, ms);
    return () => clearTimeout(t);
  }, [showPlayNextToggle, playNext, clip?.id, clip?.duration, nextClipId, onPlayNextNavigate]);

  useEffect(() => {
    if (!clip || !isAuth || !isActive) return;
    const dur = Math.max(Number(clip.duration) || 1, 1);
    pageOpenAt.current = Date.now();
    const send = (sessionFinal) => {
      if (!analyticsSessionIdRef.current) {
        analyticsSessionIdRef.current =
          typeof crypto !== 'undefined' && typeof crypto.randomUUID === 'function'
            ? crypto.randomUUID()
            : `s_${Date.now()}_${Math.random().toString(36).slice(2, 12)}`;
      }
      const t0 = pageOpenAt.current || Date.now();
      const elapsed = Math.min(dur, (Date.now() - t0) / 1000);
      clipsApi
        .engagement(clipId, {
          watched_sec: elapsed,
          duration_sec: dur,
          session_final: sessionFinal,
          session_id: analyticsSessionIdRef.current,
        })
        .catch(() => {});
    };
    const tick = setInterval(() => send(false), 8000);
    const onVis = () => {
      if (document.visibilityState === 'hidden') send(true);
    };
    document.addEventListener('visibilitychange', onVis);
    return () => {
      clearInterval(tick);
      document.removeEventListener('visibilitychange', onVis);
      send(true);
    };
  }, [clipId, clip?.duration, isAuth, isActive]);

  useEffect(() => {
    if (!clip?.broadcaster_name) return;
    streamsApi
      .isLive([clip.broadcaster_name])
      .then((data) => {
        setIsLive(!!data.live?.[clip.broadcaster_name]);
      })
      .catch(() => {});
  }, [clip?.broadcaster_name]);

  const handleLike = useCallback(
    async (isLike) => {
      if (!isAuth) return;
      try {
        const data = await clipsApi.like(clipId, isLike);
        setLikeCount(data.like_count ?? 0);
        setDislikeCount(data.dislike_count ?? 0);
        const v = data.user_vote;
        if (v === 'like') setLiked(true);
        else if (v === 'dislike') setLiked(false);
        else setLiked(null);
        setClip((prev) => (prev ? { ...prev, ...data } : prev));
      } catch {}
    },
    [clipId, isAuth],
  );

  const sendFeedback = async (action) => {
    if (!isAuth) return;
    try {
      await clipsApi.feedback(clipId, action);
      setFeedbackMsg(
        action === 'not_interested'
          ? 'Клип скрыт из персональной выдачи'
          : action === 'mute_game'
            ? 'Игра скрыта из персональной выдачи'
            : 'Канал скрыт из персональной выдачи',
      );
      setTimeout(() => setFeedbackMsg(''), 4000);
    } catch {
      setFeedbackMsg('Не удалось сохранить');
      setTimeout(() => setFeedbackMsg(''), 3000);
    }
  };

  const sendReport = async () => {
    if (!isAuth) return;
    const reason = window.prompt('Опишите нарушение (жалоба уходит модераторам):');
    if (!reason || !reason.trim()) return;
    try {
      await clipsApi.report(clipId, reason.trim());
      setFeedbackMsg('Жалоба отправлена');
      setTimeout(() => setFeedbackMsg(''), 4000);
    } catch (e) {
      setFeedbackMsg(e?.message || 'Не удалось отправить жалобу');
      setTimeout(() => setFeedbackMsg(''), 4000);
    }
  };

  const resolvedShareUrl = shareUrl ?? `${window.location.origin}/clip/${clipId}`;

  const handleShare = useCallback(() => {
    navigator.clipboard.writeText(resolvedShareUrl).then(() => {
      setCopied(true);
      setCopiedField('link');
      setTimeout(() => {
        setCopied(false);
        setCopiedField('');
      }, 2000);
    });
  }, [resolvedShareUrl]);

  const parentHost = import.meta.env.VITE_TWITCH_PARENT || window.location.hostname || 'localhost';

  const embedSrc = useMemo(() => {
    if (!clip?.embed_url) return '';
    try {
      const url = new URL(clip.embed_url);
      url.searchParams.set('parent', parentHost);
      url.searchParams.set('autoplay', 'true');
      url.searchParams.set('muted', 'false');
      return url.toString();
    } catch {
      const base = clip.embed_url;
      const sep = base.includes('?') ? '&' : '?';
      return `${base}${sep}parent=${encodeURIComponent(parentHost)}&autoplay=true&muted=false`;
    }
  }, [clip?.embed_url, parentHost]);

  const showIframe = Boolean(embedSrc && (!embedOnlyWhenActive || isActive));

  if (loading) {
    return (
      <div className={`flex items-center justify-center min-h-[40vh] ${className}`}>
        <div className="flex gap-2">
          <div className="w-3 h-3 bg-brand-500 rounded-full animate-bounce" />
          <div className="w-3 h-3 bg-brand-500 rounded-full animate-bounce" style={{ animationDelay: '0.1s' }} />
          <div className="w-3 h-3 bg-brand-500 rounded-full animate-bounce" style={{ animationDelay: '0.2s' }} />
        </div>
      </div>
    );
  }

  if (!clip) return null;

  const gameName = clip.game_name || '';
  const displayDate = formatDateRelative(clip.twitch_created_at || clip.created_at);
  const clipPageTwitch = clip.twitch_clip_id ? `https://clips.twitch.tv/${clip.twitch_clip_id}` : '';

  const pad = compact ? 'px-3 sm:px-4 py-3' : '';

  return (
    <div className={`space-y-6 ${className} ${pad}`}>
      {showBreadcrumbs && (
        <nav className="flex items-center gap-2 text-xs sm:text-sm text-gray-500 flex-wrap">
          <Link to="/" className="hover:text-white transition-colors">
            Главная
          </Link>
          <ChevronRight className="w-3.5 h-3.5 shrink-0 opacity-60" />
          {gameName && (
            <>
              <Link to={`/category/${clip.game_id}`} className="hover:text-white transition-colors truncate max-w-[40vw]">
                {gameName}
              </Link>
              <ChevronRight className="w-3.5 h-3.5 shrink-0 opacity-60" />
            </>
          )}
          <span className="text-gray-300 truncate max-w-[min(100%,36rem)]">{clip.title}</span>
        </nav>
      )}

      <div className="rounded-2xl overflow-hidden border border-white/[0.08] bg-black shadow-[0_24px_80px_-20px_rgba(0,0,0,0.85)] ring-1 ring-white/5">
        <div className="aspect-video w-full bg-black">
          {showIframe ? (
            <iframe
              key={embedSrc}
              src={embedSrc}
              className="w-full h-full"
              allow="autoplay; fullscreen; picture-in-picture"
              title={clip.title}
            />
          ) : embedSrc && !isActive ? (
            <div className="relative w-full h-full">
              <img src={clip.thumbnail_url} className="absolute inset-0 w-full h-full object-cover" alt="" />
            </div>
          ) : (
            <div className="relative w-full h-full flex items-center justify-center bg-gradient-to-br from-brand-950 to-black">
              <img src={clip.thumbnail_url} className="absolute inset-0 w-full h-full object-cover opacity-50" alt="" />
              <a
                href={clipPageTwitch}
                target="_blank"
                rel="noopener noreferrer"
                className="relative z-10 w-20 h-20 bg-[#9146FF] hover:bg-[#772CE8] rounded-full flex items-center justify-center text-white shadow-xl transition-transform hover:scale-105"
              >
                <Play className="w-9 h-9 fill-current ml-1" />
              </a>
            </div>
          )}
        </div>
      </div>

      {showPlayNextToggle && (
        <div className="flex flex-col sm:flex-row sm:items-center sm:justify-between gap-3 rounded-xl border border-brand-800/50 bg-brand-950/50 px-4 py-3">
          <div className="min-w-0">
            <p className="text-sm font-medium text-white">Автопереход к следующему</p>
            <p className="text-xs text-gray-500 mt-0.5 leading-snug">
              Следующий клип из «Похожее» откроется сам по очереди.
            </p>
          </div>
          <button
            type="button"
            role="switch"
            aria-checked={playNext}
            onClick={() => setPlayNext((v) => !v)}
            className={`relative shrink-0 h-7 w-12 rounded-full transition-colors focus:outline-none focus-visible:ring-2 focus-visible:ring-brand-400 ${
              playNext ? 'bg-brand-500' : 'bg-brand-800'
            }`}
          >
            <span
              className={`absolute top-0.5 left-0.5 h-6 w-6 rounded-full bg-white shadow transition-transform ${
                playNext ? 'translate-x-5' : 'translate-x-0'
              }`}
            />
          </button>
        </div>
      )}

      <div className="rounded-2xl border border-brand-700/40 bg-brand-900/50 backdrop-blur-sm p-6 lg:p-8 space-y-6">
        <div>
          <h1 className="text-2xl lg:text-3xl xl:text-4xl font-bold text-white leading-tight tracking-tight">
            {clip.title}
          </h1>
          <div className="mt-4 flex flex-wrap gap-3 text-sm text-gray-400">
            {gameName && (
              <Link
                to={`/category/${clip.game_id}`}
                className="inline-flex items-center gap-1.5 px-3 py-1.5 rounded-xl bg-brand-800/80 border border-brand-600/50 text-brand-200 hover:text-white hover:border-brand-500/60 transition-colors"
              >
                <Sparkles className="w-4 h-4 text-brand-400" />
                {gameName}
              </Link>
            )}
            <span className="inline-flex items-center gap-1.5 px-3 py-1.5 rounded-xl bg-brand-950/60 border border-brand-800/50">
              <Eye className="w-4 h-4" />
              {formatViews(clip.view_count)} просмотров
            </span>
            <span className="inline-flex items-center gap-1.5 px-3 py-1.5 rounded-xl bg-brand-950/60 border border-brand-800/50">
              <Calendar className="w-4 h-4" />
              {displayDate}
            </span>
            <span className="inline-flex items-center gap-1.5 px-3 py-1.5 rounded-xl bg-brand-950/60 border border-brand-800/50 tabular-nums">
              <Clock className="w-4 h-4 text-brand-400" />
              {formatDurationSec(clip.duration)}
            </span>
            {clip.language ? (
              <span className="inline-flex items-center gap-1.5 px-3 py-1.5 rounded-xl bg-brand-950/60 border border-brand-800/50 font-semibold text-gray-300 uppercase tracking-wide">
                <Languages className="w-4 h-4" />
                {String(clip.language).toUpperCase()}
              </span>
            ) : null}
          </div>
        </div>

        <div className="flex flex-wrap items-center gap-2">
          <div className="flex items-center bg-brand-950 rounded-xl overflow-hidden border border-brand-700/60">
            <button
              type="button"
              onClick={() => handleLike(true)}
              className={`px-4 py-2.5 hover:bg-brand-800 flex items-center gap-2 transition-colors border-r border-brand-800 text-sm ${
                liked === true ? 'text-brand-400' : 'text-white'
              }`}
            >
              <ThumbsUp className="w-4 h-4" /> <span className="font-bold tabular-nums">{formatViews(likeCount)}</span>
            </button>
            <button
              type="button"
              onClick={() => handleLike(false)}
              className={`px-4 py-2.5 hover:bg-brand-800 flex items-center gap-2 transition-colors text-sm ${
                liked === false ? 'text-red-400' : 'text-white'
              }`}
            >
              <ThumbsDown className="w-4 h-4" />{' '}
              <span className="font-bold tabular-nums">{formatViews(dislikeCount)}</span>
            </button>
          </div>
          <button
            type="button"
            onClick={handleShare}
            className="px-4 py-2.5 bg-brand-800 hover:bg-brand-700 text-white rounded-xl border border-brand-600/50 transition-colors flex items-center gap-2 text-sm"
          >
            <Share2 className="w-4 h-4" />{' '}
            {copied && copiedField === 'link' ? 'Скопировано' : 'Ссылка на страницу'}
          </button>
          {clipPageTwitch && (
            <a
              href={clipPageTwitch}
              target="_blank"
              rel="noopener noreferrer"
              className="px-4 py-2.5 bg-[#9146FF]/90 hover:bg-[#772CE8] text-white rounded-xl border border-violet-500/30 transition-colors flex items-center gap-2 text-sm font-semibold"
            >
              <ExternalLink className="w-4 h-4" /> Клип на Twitch
            </a>
          )}
          {isAuth && (
            <div className="flex flex-wrap items-center gap-2 w-full sm:w-auto sm:pl-2 border-t sm:border-t-0 border-brand-800/60 pt-3 sm:pt-0">
              <span className="text-gray-600 text-xs flex items-center gap-1 w-full sm:w-auto">
                <Ban className="w-3.5 h-3.5" /> Лента:
              </span>
              <button
                type="button"
                onClick={() => sendFeedback('not_interested')}
                className="px-3 py-1.5 text-xs rounded-lg border border-brand-700 bg-brand-900/80 text-gray-300 hover:text-white"
              >
                Не интересно
              </button>
              <button
                type="button"
                onClick={() => sendFeedback('mute_channel')}
                className="px-3 py-1.5 text-xs rounded-lg border border-brand-700 bg-brand-900/80 text-gray-300 hover:text-white"
              >
                Скрыть канал
              </button>
              {!!clip.game_id && (
                <button
                  type="button"
                  onClick={() => sendFeedback('mute_game')}
                  className="px-3 py-1.5 text-xs rounded-lg border border-brand-700 bg-brand-900/80 text-gray-300 hover:text-white"
                >
                  Скрыть игру
                </button>
              )}
              <button
                type="button"
                onClick={sendReport}
                className="px-3 py-1.5 text-xs rounded-lg border border-red-900/50 bg-red-950/30 text-red-200 inline-flex items-center gap-1"
              >
                <Flag className="w-3.5 h-3.5" />
                Жалоба
              </button>
            </div>
          )}
        </div>
        {feedbackMsg && <p className="text-sm text-brand-400">{feedbackMsg}</p>}

        <div className="grid sm:grid-cols-2 gap-4 pt-2 border-t border-brand-800/50">
          <div className="rounded-xl border border-brand-700/40 bg-brand-950/40 p-4 flex gap-3">
            <img
              src={clip.broadcaster_avatar || `https://api.dicebear.com/7.x/avataaars/svg?seed=${clip.broadcaster_name}`}
              className="w-14 h-14 rounded-xl object-cover border border-brand-600/40 shrink-0"
              alt=""
            />
            <div className="min-w-0">
              <p className="text-[10px] uppercase tracking-wider text-gray-500 font-bold mb-1">Канал</p>
              <div className="flex items-center gap-2 flex-wrap">
                <a
                  href={`https://www.twitch.tv/${encodeURIComponent(clip.broadcaster_name)}`}
                  target="_blank"
                  rel="noopener noreferrer"
                  className="text-lg font-bold text-white hover:text-[#a970ff] transition-colors truncate"
                >
                  {clip.broadcaster_name}
                </a>
                {isLive && (
                  <span className="inline-flex items-center gap-1 bg-red-600/90 px-2 py-0.5 rounded-full text-[10px] font-bold text-white uppercase">
                    Live
                  </span>
                )}
              </div>
              <p className="text-xs text-gray-500 mt-0.5">{isLive ? 'Сейчас в эфире на Twitch' : 'Оффлайн'}</p>
            </div>
          </div>

          <div className="rounded-xl border border-brand-700/40 bg-brand-950/40 p-4 flex gap-3">
            <div className="w-14 h-14 rounded-xl bg-gradient-to-br from-violet-600/40 to-fuchsia-600/30 border border-violet-500/30 flex items-center justify-center shrink-0">
              <Clapperboard className="w-7 h-7 text-violet-300" />
            </div>
            <div className="min-w-0">
              <p className="text-[10px] uppercase tracking-wider text-gray-500 font-bold mb-1">Создатель клипа</p>
              {clip.creator_login ? (
                <>
                  <a
                    href={`https://www.twitch.tv/${encodeURIComponent(clip.creator_login)}`}
                    target="_blank"
                    rel="noopener noreferrer"
                    className="text-lg font-bold text-white hover:text-violet-300 transition-colors truncate block"
                  >
                    {clip.creator_login}
                  </a>
                  {clip.creator_site?.username && (
                    <Link
                      to={`/u/${encodeURIComponent(clip.creator_site.username)}`}
                      className="text-xs text-brand-400 hover:text-brand-300 mt-1 inline-block"
                    >
                      Профиль на ClipStream → @{clip.creator_site.username}
                    </Link>
                  )}
                </>
              ) : (
                <p className="text-sm text-gray-500">Нет данных о создателе</p>
              )}
            </div>
          </div>
        </div>
      </div>

      <CommentSection clipId={Number(clipId)} />
    </div>
  );
}
