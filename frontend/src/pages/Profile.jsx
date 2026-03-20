import { useEffect, useState, useCallback, useMemo } from 'react';
import { useParams, Link, useNavigate, useSearchParams } from 'react-router-dom';
import {
  User,
  Heart,
  Clock,
  Users,
  MessageSquare,
  Star,
  MoonStar,
  Trophy,
  Wrench,
  Crown,
  UserPlus,
  UserMinus,
  Share2,
  Sparkles,
  Settings,
  Search,
  Loader2,
  Clapperboard,
} from 'lucide-react';
import { social as socialApi, userSettings } from '../api/client';
import useAuthStore from '../store/useAuthStore';
import { useClipLangStore } from '../store/useClipLangStore';
import ClipCard from '../components/clips/ClipCard';

const BADGE_LABELS = { 0: '—', 1: 'Rising Star', 2: 'Pro', 3: 'Legend' };

const TAB_KEYS = ['overview', 'followers', 'following', 'clips', 'activity', 'friends', 'settings'];

function avatarUrl(u) {
  if (u?.avatar_url) return u.avatar_url;
  const seed = encodeURIComponent(u?.username || 'user');
  return `https://api.dicebear.com/7.x/avataaars/svg?seed=${seed}`;
}

export default function Profile() {
  const { username: paramUsername } = useParams();
  const navigate = useNavigate();
  const [searchParams, setSearchParams] = useSearchParams();
  const { user, isAuth, profile, fetchMe } = useAuthStore();
  const [data, setData] = useState(null);
  const [err, setErr] = useState('');
  const [loading, setLoading] = useState(true);
  const [followBusy, setFollowBusy] = useState(false);

  const [followers, setFollowers] = useState([]);
  const [following, setFollowing] = useState([]);
  const [authorClips, setAuthorClips] = useState([]);
  const [watchActivity, setWatchActivity] = useState([]);
  const [friendsClips, setFriendsClips] = useState([]);
  const [tabLoading, setTabLoading] = useState(false);
  const [searchQ, setSearchQ] = useState('');
  const [searchHits, setSearchHits] = useState([]);
  const [searchBusy, setSearchBusy] = useState(false);

  const clipLangKey = useClipLangStore((s) => s.codes.join(','));
  const [settingsForm, setSettingsForm] = useState({
    hide_mature: true,
    mute_keywords: [],
  });
  const [kwInput, setKwInput] = useState('');
  const [settingsSaved, setSettingsSaved] = useState(false);

  const staffAccess = user?.role === 1 || user?.role === 2;
  const isAdmin = user?.role === 2;

  const targetUsername = paramUsername?.trim() || '';

  const tab = useMemo(() => {
    const t = (searchParams.get('tab') || 'overview').toLowerCase();
    return TAB_KEYS.includes(t) ? t : 'overview';
  }, [searchParams]);

  const setTab = (next) => {
    const p = new URLSearchParams(searchParams);
    if (next === 'overview') p.delete('tab');
    else p.set('tab', next);
    setSearchParams(p, { replace: true });
  };

  useEffect(() => {
    if (isAuth && !profile) fetchMe();
  }, [isAuth, profile, fetchMe]);

  const load = useCallback(() => {
    if (!targetUsername) return;
    setLoading(true);
    setErr('');
    socialApi
      .publicProfile(targetUsername)
      .then(setData)
      .catch((e) => setErr(e.message || 'Не удалось загрузить профиль'))
      .finally(() => setLoading(false));
  }, [targetUsername]);

  useEffect(() => {
    load();
  }, [load]);

  const isSelf = isAuth && user && data && user.id === data.id;

  const visibleTabs = useMemo(() => {
    const base = [
      { id: 'overview', label: 'Обзор' },
      { id: 'followers', label: 'Подписчики' },
      { id: 'following', label: 'Подписки' },
      { id: 'clips', label: 'Клипы' },
    ];
    if (isSelf) {
      base.push(
        { id: 'activity', label: 'Активность' },
        { id: 'friends', label: 'Друзья смотрят' },
        { id: 'settings', label: 'Лента и поиск' },
      );
    }
    return base;
  }, [isSelf]);

  useEffect(() => {
    if (!data?.id) return;
    if (!isSelf && ['activity', 'friends', 'settings'].includes(tab)) setTab('overview');
  }, [tab, isSelf, data?.id]);

  useEffect(() => {
    if (!data?.id) return;
    let cancelled = false;
    const run = async () => {
      setTabLoading(true);
      try {
        if (tab === 'followers') {
          const r = await socialApi.followers(data.id, 'limit=50&offset=0');
          if (!cancelled) setFollowers(r.users || []);
        } else if (tab === 'following') {
          const r = await socialApi.following(data.id, 'limit=50&offset=0');
          if (!cancelled) setFollowing(r.users || []);
        } else if (tab === 'clips') {
          const r = await socialApi.userClips(data.id, 'limit=24&offset=0');
          if (!cancelled) setAuthorClips(r.clips || []);
        } else if (tab === 'activity' && isSelf) {
          const r = await socialApi.watchActivity(data.id, 'limit=40&offset=0');
          if (!cancelled) setWatchActivity(r.activity || []);
        } else if (tab === 'friends' && isSelf) {
          const r = await socialApi.friendsFeed('limit=20&offset=0');
          if (!cancelled) setFriendsClips(r.clips || []);
        } else if (tab === 'settings' && isSelf) {
          const sdata = await userSettings.get();
          if (!cancelled) {
            setSettingsForm({
              hide_mature: sdata.hide_mature !== false,
              mute_keywords: Array.isArray(sdata.mute_keywords) ? sdata.mute_keywords : [],
            });
          }
        }
      } catch {
        if (!cancelled && tab === 'followers') setFollowers([]);
        if (!cancelled && tab === 'following') setFollowing([]);
        if (!cancelled && tab === 'clips') setAuthorClips([]);
        if (!cancelled && tab === 'activity') setWatchActivity([]);
        if (!cancelled && tab === 'friends') setFriendsClips([]);
      } finally {
        if (!cancelled) setTabLoading(false);
      }
    };
    if (['followers', 'following', 'clips', 'activity', 'friends', 'settings'].includes(tab)) {
      run();
    }
    return () => {
      cancelled = true;
    };
  }, [tab, data?.id, isSelf, clipLangKey]);

  const saveSettings = async () => {
    await userSettings.save({
      hide_mature: settingsForm.hide_mature,
      mute_keywords: settingsForm.mute_keywords,
      clip_languages: useClipLangStore.getState().codes,
    });
    setSettingsSaved(true);
    setTimeout(() => setSettingsSaved(false), 2500);
  };

  const addKeyword = () => {
    const t = kwInput.trim();
    if (!t || settingsForm.mute_keywords.includes(t)) return;
    if (settingsForm.mute_keywords.length >= 32) return;
    setSettingsForm((s) => ({
      ...s,
      mute_keywords: [...s.mute_keywords, t.slice(0, 64)],
    }));
    setKwInput('');
  };

  const removeKeyword = (w) => {
    setSettingsForm((s) => ({
      ...s,
      mute_keywords: s.mute_keywords.filter((x) => x !== w),
    }));
  };

  const runUserSearch = async () => {
    const q = searchQ.trim();
    if (q.length < 2) {
      setSearchHits([]);
      return;
    }
    setSearchBusy(true);
    try {
      const r = await socialApi.searchUsers(q, 20);
      setSearchHits(r.users || []);
    } catch {
      setSearchHits([]);
    } finally {
      setSearchBusy(false);
    }
  };

  const toggleFollow = async () => {
    if (!isAuth || !data || isSelf) return;
    setFollowBusy(true);
    try {
      if (data.is_following) await socialApi.unfollow(data.id);
      else await socialApi.follow(data.id);
      await load();
    } catch (e) {
      setErr(e.message || 'Ошибка подписки');
    } finally {
      setFollowBusy(false);
    }
  };

  const copyProfileUrl = () => {
    if (!data?.username) return;
    const url = `${window.location.origin}/u/${encodeURIComponent(data.username)}`;
    navigator.clipboard.writeText(url).catch(() => {});
  };

  const goClip = (clip) => navigate(`/clip/${clip.id}`);

  if (!targetUsername) {
    return (
      <div className="p-8 max-w-lg mx-auto text-center">
        <p className="text-gray-400 text-sm mb-4">
          Неверный адрес. Используйте <span className="text-brand-400">/u/ник</span>
        </p>
        <Link to="/" className="text-brand-400 text-sm hover:underline">На главную</Link>
      </div>
    );
  }

  if (loading) {
    return (
      <div className="p-8 flex justify-center text-gray-400">
        <Loader2 className="w-8 h-8 animate-spin" />
      </div>
    );
  }

  if (err || !data) {
    return (
      <div className="p-8 max-w-lg mx-auto text-center">
        <p className="text-red-400 text-sm mb-4">{err || 'Пользователь не найден'}</p>
        <Link to="/" className="text-brand-400 text-sm hover:underline">На главную</Link>
      </div>
    );
  }

  const stats = data.stats || {};
  const achievements = isSelf && profile?.badges ? profile.badges : [];

  return (
    <div className="p-4 sm:p-6 lg:p-8 max-w-5xl mx-auto pb-16">
      <div className="rounded-2xl border border-brand-700/50 bg-gradient-to-br from-brand-900/80 to-brand-950/90 p-6 sm:p-8 mb-6">
        <div className="flex flex-col lg:flex-row lg:items-start gap-6">
          <div className="flex items-start gap-4 min-w-0">
            <div className="w-24 h-24 rounded-2xl bg-gradient-to-tr from-brand-500 to-fuchsia-500 p-[3px] shrink-0 shadow-lg shadow-brand-500/20">
              <img
                src={avatarUrl(data)}
                alt=""
                className="rounded-2xl bg-brand-900 w-full h-full object-cover"
              />
            </div>
            <div className="min-w-0">
              <div className="flex items-center gap-2 flex-wrap">
                <h1 className="text-2xl sm:text-3xl font-bold text-white tracking-tight">{data.username}</h1>
                {isSelf && (user?.role ?? 0) === 2 && (
                  <span className="text-[10px] font-bold uppercase tracking-wide px-2 py-0.5 rounded border border-red-500/50 bg-red-500/15 text-red-300">
                    Админ
                  </span>
                )}
                {isSelf && (user?.role ?? 0) === 1 && (
                  <span className="text-[10px] font-bold uppercase tracking-wide px-2 py-0.5 rounded border border-amber-500/50 bg-amber-500/15 text-amber-200">
                    Модератор
                  </span>
                )}
                {isSelf && (user?.role ?? 0) === 3 && (
                  <span className="text-[10px] font-bold uppercase tracking-wide px-2 py-0.5 rounded border border-fuchsia-500/50 bg-fuchsia-500/15 text-fuchsia-200">
                    VIP
                  </span>
                )}
              </div>
              <p className="text-gray-400 text-sm mt-1">
                Репутация <span className="text-brand-300 font-semibold">{data.reputation ?? 0}</span>
                {' · '}
                {BADGE_LABELS[data.badge] ?? '—'}
              </p>
              {data.twitch_id && <p className="text-xs text-gray-500 mt-1">Twitch привязан</p>}
              {isSelf && user?.email && <p className="text-gray-500 text-xs mt-1">{user.email}</p>}
              <div className="flex flex-wrap gap-2 mt-3">
                <button
                  type="button"
                  onClick={copyProfileUrl}
                  className="inline-flex items-center gap-1.5 text-xs px-3 py-1.5 rounded-lg bg-brand-800/80 border border-brand-600/50 text-gray-300 hover:text-white"
                >
                  <Share2 className="w-3.5 h-3.5" /> Скопировать ссылку
                </button>
              </div>
            </div>
          </div>
          <div className="flex flex-wrap gap-2 lg:ml-auto lg:justify-end">
            {isSelf && isAdmin && (
              <Link
                to="/admin"
                className="inline-flex items-center gap-2 px-4 py-2.5 rounded-xl bg-red-500/15 border border-red-500/35 text-red-200 text-sm font-semibold hover:bg-red-500/25"
              >
                <Crown className="w-4 h-4" /> Админ
              </Link>
            )}
            {isSelf && staffAccess && (
              <Link
                to="/staff"
                className="inline-flex items-center gap-2 px-4 py-2.5 rounded-xl bg-amber-500/15 border border-amber-500/35 text-amber-200 text-sm font-semibold hover:bg-amber-500/25"
              >
                <Wrench className="w-4 h-4" /> Консоль
              </Link>
            )}
            {isAuth && !isSelf && (
              <button
                type="button"
                disabled={followBusy}
                onClick={toggleFollow}
                className={`inline-flex items-center gap-2 px-5 py-2.5 rounded-xl text-sm font-semibold transition-colors ${
                  data.is_following
                    ? 'bg-brand-800 border border-brand-600 text-gray-200 hover:bg-brand-700'
                    : 'bg-brand-500 text-white hover:bg-brand-400'
                } disabled:opacity-50`}
              >
                {followBusy ? (
                  <Loader2 className="w-4 h-4 animate-spin" />
                ) : data.is_following ? (
                  <>
                    <UserMinus className="w-4 h-4" /> Отписаться
                  </>
                ) : (
                  <>
                    <UserPlus className="w-4 h-4" /> Подписаться
                  </>
                )}
              </button>
            )}
          </div>
        </div>

        <div className="grid grid-cols-2 sm:grid-cols-4 gap-3 mt-8 pt-6 border-t border-brand-700/40">
          <div className="rounded-xl bg-brand-950/50 px-4 py-3 border border-brand-700/30">
            <p className="text-2xl font-bold text-white">{stats.followers_count ?? 0}</p>
            <p className="text-xs text-gray-500">Подписчиков</p>
          </div>
          <div className="rounded-xl bg-brand-950/50 px-4 py-3 border border-brand-700/30">
            <p className="text-2xl font-bold text-white">{stats.following_count ?? 0}</p>
            <p className="text-xs text-gray-500">Подписок</p>
          </div>
          <div className="rounded-xl bg-brand-950/50 px-4 py-3 border border-brand-700/30">
            <p className="text-2xl font-bold text-white">{stats.comments_count ?? 0}</p>
            <p className="text-xs text-gray-500">Комментариев</p>
          </div>
          <div className="rounded-xl bg-brand-950/50 px-4 py-3 border border-violet-900/40">
            <p className="text-2xl font-bold text-white">{stats.clips_created_count ?? 0}</p>
            <p className="text-xs text-gray-500">Клипов создано</p>
          </div>
        </div>
      </div>

      {isSelf && (user?.account_blocked || user?.chat_banned_until) && (
        <div className="mb-6 rounded-xl border border-amber-500/40 bg-amber-950/30 px-4 py-3 text-sm text-amber-100">
          {user?.account_blocked && <p>Аккаунт заблокирован модерацией.</p>}
          {!user?.account_blocked && user?.chat_banned_until && (
            <p>Ограничение чата до {user.chat_banned_until}</p>
          )}
        </div>
      )}

      <div className="flex gap-1 overflow-x-auto pb-2 mb-4 scrollbar-thin scrollbar-thumb-brand-700">
        {visibleTabs.map(({ id, label }) => (
          <button
            key={id}
            type="button"
            onClick={() => setTab(id)}
            className={`shrink-0 px-4 py-2 rounded-xl text-sm font-medium transition-colors ${
              tab === id
                ? 'bg-brand-500 text-white shadow-lg shadow-brand-500/25'
                : 'bg-brand-800/60 text-gray-400 hover:text-white border border-brand-700/50'
            }`}
          >
            {label}
          </button>
        ))}
      </div>

      {tab === 'overview' && (
        <>
          <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 gap-4 mb-6">
            <div className="bg-brand-800 rounded-xl p-5 border border-violet-900/35">
              <Clapperboard className="w-5 h-5 text-violet-400 mb-3" />
              <p className="text-2xl font-bold text-white">{stats.clips_created_count ?? 0}</p>
              <p className="text-xs text-gray-400 mt-1">Клипов на Twitch (импорт на сайт)</p>
            </div>
            <div className="bg-brand-800 rounded-xl p-5 border border-brand-700/50">
              <Heart className="w-5 h-5 text-brand-500 mb-3" />
              <p className="text-2xl font-bold text-white">{stats.liked_count || 0}</p>
              <p className="text-xs text-gray-400 mt-1">Лайков поставлено</p>
            </div>
            <div className="bg-brand-800 rounded-xl p-5 border border-brand-700/50">
              <Clock className="w-5 h-5 text-brand-500 mb-3" />
              <p className="text-2xl font-bold text-white">{stats.unique_viewed_clips || 0}</p>
              <p className="text-xs text-gray-400 mt-1">Уникальных клипов</p>
            </div>
            <div className="bg-brand-800 rounded-xl p-5 border border-brand-700/50">
              <MessageSquare className="w-5 h-5 text-emerald-400 mb-3" />
              <p className="text-2xl font-bold text-white">{stats.comments_count || 0}</p>
              <p className="text-xs text-gray-400 mt-1">Комментариев</p>
            </div>
            <div className="bg-brand-800 rounded-xl p-5 border border-brand-700/50">
              <Star className="w-5 h-5 text-sky-400 mb-3" />
              <p className="text-base font-bold text-white truncate">{stats.favorite_game_name || '—'}</p>
              <p className="text-xs text-gray-400 mt-1">Любимая категория</p>
            </div>
            <div className="bg-brand-800 rounded-xl p-5 border border-brand-700/50">
              <MoonStar className="w-5 h-5 text-indigo-400 mb-3" />
              <p className="text-2xl font-bold text-white">{stats.night_views || 0}</p>
              <p className="text-xs text-gray-400 mt-1">Ночных просмотров</p>
            </div>
          </div>
          <div className="bg-brand-800 rounded-xl p-5 border border-brand-700/50">
            <h3 className="text-white font-bold flex items-center gap-2 mb-3">
              <Trophy className="w-4 h-4 text-yellow-400" />
              История бейджей
            </h3>
            {!data.badge_history || data.badge_history.length === 0 ? (
              <p className="text-sm text-gray-400">Пока нет записей.</p>
            ) : (
              <ul className="space-y-2 text-sm">
                {data.badge_history.map((row) => (
                  <li
                    key={row.id}
                    className="flex justify-between gap-4 border-b border-brand-700/40 pb-2 last:border-0"
                  >
                    <span className="text-gray-300">
                      <span className="text-white font-medium">{row.badge}</span>{' '}
                      <span className={row.event_type === 'granted' ? 'text-emerald-400' : 'text-amber-400'}>
                        ({row.event_type})
                      </span>
                    </span>
                    <span className="text-gray-500 shrink-0">{row.created_at}</span>
                  </li>
                ))}
              </ul>
            )}
          </div>
          {achievements.length > 0 && (
            <div className="mt-6 bg-brand-800 rounded-xl p-5 border border-brand-700/50">
              <h3 className="text-white font-bold flex items-center gap-2 mb-3">
                <Sparkles className="w-4 h-4 text-violet-400" />
                Достижения
              </h3>
              <div className="grid grid-cols-1 md:grid-cols-2 gap-3">
                {achievements.map((b) => {
                  const pct = b.target > 0 ? Math.min(100, Math.round((b.progress / b.target) * 100)) : 0;
                  return (
                    <div
                      key={b.id}
                      className={`rounded-xl border p-3 ${b.unlocked ? 'border-brand-500/70 bg-brand-700/30' : 'border-brand-700/60 bg-brand-900/40'}`}
                    >
                      <div className="flex items-center justify-between gap-2 mb-1.5">
                        <p className="text-sm font-semibold text-white">{b.title}</p>
                        <span className={`text-[10px] px-2 py-0.5 rounded-full ${b.unlocked ? 'bg-emerald-500/20 text-emerald-300' : 'bg-gray-700 text-gray-300'}`}>
                          {b.unlocked ? 'Получен' : 'В процессе'}
                        </span>
                      </div>
                      <p className="text-xs text-gray-400 mb-2">{b.description}</p>
                      <div className="w-full h-1.5 rounded bg-brand-900 overflow-hidden">
                        <div className="h-full bg-brand-500" style={{ width: `${pct}%` }} />
                      </div>
                    </div>
                  );
                })}
              </div>
            </div>
          )}
        </>
      )}

      {(tab === 'followers' || tab === 'following') && (
        <div className="rounded-xl border border-brand-700/50 bg-brand-800/40 p-4">
          {tabLoading ? (
            <div className="flex justify-center py-12 text-gray-500">
              <Loader2 className="w-8 h-8 animate-spin" />
            </div>
          ) : (
            <ul className="grid gap-2 sm:grid-cols-2">
              {(tab === 'followers' ? followers : following).map((u) => (
                <li key={u.id}>
                  <Link
                    to={`/u/${encodeURIComponent(u.username)}`}
                    className="flex items-center gap-3 p-3 rounded-xl border border-brand-700/40 hover:bg-brand-800/80 transition-colors"
                  >
                    <img src={avatarUrl(u)} alt="" className="w-11 h-11 rounded-full object-cover bg-brand-900" />
                    <div className="min-w-0">
                      <p className="text-white font-medium truncate">{u.username}</p>
                      {u.since && <p className="text-[11px] text-gray-500">с {u.since}</p>}
                    </div>
                  </Link>
                </li>
              ))}
            </ul>
          )}
          {!tabLoading && (tab === 'followers' ? followers : following).length === 0 && (
            <p className="text-center text-gray-500 text-sm py-8">Пока пусто.</p>
          )}
        </div>
      )}

      {tab === 'clips' && (
        <div>
          {tabLoading ? (
            <div className="flex justify-center py-16 text-gray-500">
              <Loader2 className="w-8 h-8 animate-spin" />
            </div>
          ) : authorClips.length === 0 ? (
            <p className="text-center text-gray-500 text-sm py-12">Нет клипов с привязкой к авторам на платформе.</p>
          ) : (
            <div className="grid grid-cols-1 sm:grid-cols-2 xl:grid-cols-3 gap-4">
              {authorClips.map((clip, i) => (
                <ClipCard key={clip.id} clip={clip} index={i} onClick={goClip} />
              ))}
            </div>
          )}
        </div>
      )}

      {tab === 'activity' && isSelf && (
        <div className="rounded-xl border border-brand-700/50 bg-brand-800/40 p-4">
          <p className="text-xs text-gray-500 mb-4">Видят только вы — последние просмотры на платформе.</p>
          {tabLoading ? (
            <div className="flex justify-center py-12">
              <Loader2 className="w-8 h-8 animate-spin text-gray-500" />
            </div>
          ) : (
            <ul className="space-y-2">
              {watchActivity.map((row, idx) => (
                <li key={`${row.clip_id}-${idx}`}>
                  <button
                    type="button"
                    onClick={() => navigate(`/clip/${row.clip_id}`)}
                    className="w-full flex gap-3 p-3 rounded-xl border border-brand-700/30 hover:bg-brand-800/70 text-left"
                  >
                    <img
                      src={row.thumbnail_url || avatarUrl({ username: row.broadcaster_name })}
                      alt=""
                      className="w-28 rounded-lg object-cover aspect-video bg-brand-900 shrink-0"
                    />
                    <div className="min-w-0">
                      <p className="text-sm text-white font-medium line-clamp-2">{row.title}</p>
                      <p className="text-xs text-gray-500 mt-1">{row.broadcaster_name}</p>
                      <p className="text-[11px] text-gray-600 mt-0.5">{row.viewed_at}</p>
                    </div>
                  </button>
                </li>
              ))}
            </ul>
          )}
          {!tabLoading && watchActivity.length === 0 && (
            <p className="text-center text-gray-500 text-sm py-8">История просмотров пуста.</p>
          )}
        </div>
      )}

      {tab === 'friends' && isSelf && (
        <div>
          <p className="text-sm text-gray-400 mb-4">
            Клипы, которые недавно смотрели пользователи, на которых вы подписаны в ClipStream.
          </p>
          {tabLoading ? (
            <div className="flex justify-center py-16">
              <Loader2 className="w-8 h-8 animate-spin text-gray-500" />
            </div>
          ) : friendsClips.length === 0 ? (
            <p className="text-gray-500 text-sm text-center py-12">Подпишитесь на людей и смотрите клипы — здесь появится лента.</p>
          ) : (
            <div className="grid grid-cols-1 sm:grid-cols-2 xl:grid-cols-3 gap-4">
              {friendsClips.map((clip, i) => (
                <ClipCard key={clip.id} clip={clip} index={i} onClick={goClip} />
              ))}
            </div>
          )}
        </div>
      )}

      {tab === 'settings' && isSelf && (
        <div className="rounded-xl border border-brand-700/50 bg-brand-800/40 p-6 space-y-8">
          <div>
            <h3 className="text-white font-semibold flex items-center gap-2 mb-3">
              <Settings className="w-4 h-4" /> Лента
            </h3>
            <label className="flex items-center gap-2 text-sm text-gray-300 cursor-pointer">
              <input
                type="checkbox"
                checked={settingsForm.hide_mature}
                onChange={(e) => setSettingsForm((s) => ({ ...s, hide_mature: e.target.checked }))}
                className="rounded border-brand-600"
              />
              Скрывать клипы с меткой 18+ / mature
            </label>

            <div className="mt-6 pt-4 border-t border-brand-700/40">
              <p className="text-sm text-white font-medium mb-1">Языки трансляций</p>
              <p className="text-xs text-gray-500">
                Выбор языков делается в шапке сайта (кнопка «Язык» слева от профиля). Настройка действует на все ленты
                клипов и сохраняется в аккаунте.
              </p>
            </div>

            <p className="text-xs text-gray-500 mt-6 mb-2">Слова в заголовке (не показывать клипы, если заголовок содержит):</p>
            <div className="flex flex-wrap gap-2 mb-2">
              {settingsForm.mute_keywords.map((w) => (
                <button
                  key={w}
                  type="button"
                  onClick={() => removeKeyword(w)}
                  className="text-xs px-2 py-1 rounded-lg bg-brand-900 border border-brand-600 text-gray-300 hover:border-red-500/50"
                >
                  {w} ×
                </button>
              ))}
            </div>
            <div className="flex gap-2">
              <input
                value={kwInput}
                onChange={(e) => setKwInput(e.target.value)}
                onKeyDown={(e) => e.key === 'Enter' && (e.preventDefault(), addKeyword())}
                placeholder="ключевое слово"
                className="flex-1 bg-brand-900 border border-brand-700 rounded-lg px-3 py-2 text-sm text-white"
              />
              <button type="button" onClick={addKeyword} className="px-4 py-2 rounded-lg bg-brand-600 text-white text-sm">
                Добавить
              </button>
            </div>
            <button
              type="button"
              onClick={saveSettings}
              className="mt-4 px-5 py-2.5 rounded-xl bg-brand-500 text-white text-sm font-semibold hover:bg-brand-400"
            >
              Сохранить настройки ленты
            </button>
            {settingsSaved && <p className="text-emerald-400 text-sm mt-2">Сохранено</p>}
          </div>

          <div className="border-t border-brand-700/50 pt-6">
            <h3 className="text-white font-semibold flex items-center gap-2 mb-3">
              <Search className="w-4 h-4" /> Найти пользователей
            </h3>
            <div className="flex gap-2">
              <input
                value={searchQ}
                onChange={(e) => setSearchQ(e.target.value)}
                onKeyDown={(e) => e.key === 'Enter' && (e.preventDefault(), runUserSearch())}
                placeholder="минимум 2 символа"
                className="flex-1 bg-brand-900 border border-brand-700 rounded-lg px-3 py-2 text-sm text-white"
              />
              <button
                type="button"
                onClick={runUserSearch}
                disabled={searchBusy}
                className="px-4 py-2 rounded-lg bg-brand-700 text-white text-sm flex items-center gap-2"
              >
                {searchBusy ? <Loader2 className="w-4 h-4 animate-spin" /> : <Search className="w-4 h-4" />}
              </button>
            </div>
            <ul className="mt-4 space-y-2">
              {searchHits.map((u) => (
                <li key={u.id}>
                  <Link
                    to={`/u/${encodeURIComponent(u.username)}`}
                    className="flex items-center gap-3 p-2 rounded-lg hover:bg-brand-800/70"
                  >
                    <img src={avatarUrl(u)} alt="" className="w-9 h-9 rounded-full" />
                    <span className="text-white text-sm">{u.username}</span>
                  </Link>
                </li>
              ))}
            </ul>
          </div>
        </div>
      )}
    </div>
  );
}
