/**
 * Единственная реализация профиля сайта: маршруты /u/:username и /channel/:login (и ?tab=…).
 */
import { useEffect, useState, useCallback, useMemo } from 'react';
import { useParams, Link, useNavigate, useSearchParams } from 'react-router-dom';
import {
  Heart,
  Clock,
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
  Check,
  X,
  Users,
  ExternalLink,
  Radio,
  Rss,
  LayoutGrid,
  LogIn,
  Circle,
} from 'lucide-react';
import { social as socialApi, userSettings, channels as channelsApi } from '../api/client';
import useAuthStore from '../store/useAuthStore';
import { useClipLangStore } from '../store/useClipLangStore';
import ClipCard from '../components/clips/ClipCard';
import TwitchChannelPanel from '../components/channel/TwitchChannelPanel';
import SocialProfileLayout from '../components/layout/SocialProfileLayout';

const TAB_ICONS = {
  overview: LayoutGrid,
  friendships: Users,
  wall: Rss,
  clips: Clapperboard,
  twitch: Radio,
  activity: Clock,
  friends_feed: Heart,
  settings: Settings,
};

const BADGE_LABELS = { 0: '—', 1: 'Rising Star', 2: 'Pro', 3: 'Legend' };

const ACHIEVEMENT_TITLES = {
  first_clip: 'Первые шаги',
  explorer: 'Исследователь',
  rising_star: 'Rising Star',
  critic: 'Критик',
  talker: 'Комментатор',
  first_friend: 'Знакомство',
  social_circle: 'Круг друзей',
  social_star: 'Свой человек',
  wall_starter: 'Стена жива',
  night_owl: 'Night Owl',
  legend: 'Legend',
};

const TAB_KEYS = ['overview', 'friendships', 'wall', 'clips', 'twitch', 'activity', 'friends_feed', 'settings'];

/** Профиль страницы /channel/:login без аккаунта на сайте — тот же объект data, что у /u/:username */
function buildGuestProfileFromChannel(cp, routeLogin) {
  const tw = cp?.twitch;
  const resolvedLogin = String(tw?.login || routeLogin || '').trim() || routeLogin;
  const displayTitle = tw?.display_name || resolvedLogin;
  const avatar = tw?.profile_image_url || null;
  const found = cp?.found !== false;
  return {
    id: null,
    username: displayTitle,
    avatar_url: avatar,
    reputation: 0,
    badge: 0,
    twitch_login: resolvedLogin,
    twitch_id: tw?.id || null,
    twitch_channel: found && tw ? { twitch: tw, clipstream_clips: cp?.clipstream_clips ?? 0 } : null,
    stats: {
      friends_count: 0,
      wall_posts_count: 0,
      comments_count: 0,
      clips_created_count: cp?.clipstream_clips ?? 0,
      liked_count: 0,
      unique_viewed_clips: 0,
      favorite_game_name: null,
      night_views: 0,
    },
    achievements_earned: [],
    badge_history: [],
    friendship: 'none',
    is_channel_guest: true,
    channel_route_login: routeLogin,
    channel_api_found: found,
    used_clipstream_via_twitch: false,
    on_site_now: false,
  };
}

function WallPostBody({ body, mentions }) {
  const userByLower = useMemo(() => {
    const m = new Map();
    (mentions || []).forEach((x) => {
      if (x.kind === 'user' && x.username) m.set(String(x.username).toLowerCase(), x.username);
    });
    return m;
  }, [mentions]);

  const nodes = useMemo(() => {
    const s = body || '';
    const re = /@(?:clip\/\d+|[A-Za-z0-9_]{2,32})/g;
    const out = [];
    let last = 0;
    let mm;
    while ((mm = re.exec(s)) !== null) {
      if (mm.index > last) out.push({ type: 'text', text: s.slice(last, mm.index), key: `t-${last}` });
      const tok = mm[0];
      if (tok.startsWith('@clip/')) {
        const id = tok.slice(6);
        out.push(/^\d+$/.test(id) ? { type: 'clip', id, key: `c-${mm.index}` } : { type: 'text', text: tok, key: `x-${mm.index}` });
      } else {
        const uname = tok.slice(1);
        const real = userByLower.get(uname.toLowerCase());
        out.push(
          real
            ? { type: 'user', username: real, key: `u-${mm.index}` }
            : { type: 'text', text: tok, key: `r-${mm.index}` },
        );
      }
      last = mm.index + tok.length;
    }
    if (last < s.length) out.push({ type: 'text', text: s.slice(last), key: 't-end' });
    return out;
  }, [body, userByLower]);

  return (
    <span className="whitespace-pre-wrap break-words">
      {nodes.map((n) =>
        n.type === 'text' ? (
          <span key={n.key}>{n.text}</span>
        ) : n.type === 'clip' ? (
          <Link
            key={n.key}
            to={`/clip/${n.id}`}
            className="text-brand-400 hover:underline font-medium"
          >
            @clip/{n.id}
          </Link>
        ) : (
          <Link
            key={n.key}
            to={`/u/${encodeURIComponent(n.username)}`}
            className="text-sky-400 hover:underline font-medium"
          >
            @{n.username}
          </Link>
        ),
      )}
    </span>
  );
}

function avatarUrl(u) {
  if (u?.avatar_url) return u.avatar_url;
  const seed = encodeURIComponent(u?.username || 'user');
  return `https://api.dicebear.com/7.x/avataaars/svg?seed=${seed}`;
}

function ProfileSection({ eyebrow, title, description, children, className = '' }) {
  return (
    <section className={`rounded-2xl border border-brand-700/45 bg-brand-900/35 backdrop-blur-sm overflow-hidden ${className}`}>
      {(eyebrow || title || description) && (
        <div className="px-4 sm:px-6 pt-4 sm:pt-5 pb-3 border-b border-brand-800/80">
          {eyebrow ? (
            <p className="text-[10px] font-semibold uppercase tracking-[0.14em] text-brand-400/90 mb-1">{eyebrow}</p>
          ) : null}
          {title ? <h2 className="text-base sm:text-lg lg:text-xl font-bold text-white">{title}</h2> : null}
          {description ? (
            <p className="text-xs sm:text-sm text-gray-500 mt-1 max-w-[min(100%,40rem)] text-pretty">{description}</p>
          ) : null}
        </div>
      )}
      <div className="p-4 sm:p-6">{children}</div>
    </section>
  );
}

export default function Profile() {
  const { username: paramUsername, login: paramChannelLogin } = useParams();
  const navigate = useNavigate();
  const [searchParams, setSearchParams] = useSearchParams();
  const { user, isAuth, profile, fetchMe } = useAuthStore();
  const [data, setData] = useState(null);
  const [err, setErr] = useState('');
  const [loading, setLoading] = useState(true);
  const [friendBusy, setFriendBusy] = useState(false);

  const [friendList, setFriendList] = useState([]);
  const [incomingFriends, setIncomingFriends] = useState([]);
  const [outgoingFriends, setOutgoingFriends] = useState([]);
  const [wallPosts, setWallPosts] = useState([]);
  const [wallText, setWallText] = useState('');
  const [wallBusy, setWallBusy] = useState(false);
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

  const channelRouteLogin = paramChannelLogin ? decodeURIComponent(String(paramChannelLogin)).trim() : '';
  const isChannelRoute = channelRouteLogin.length > 0;
  const targetUsername = !isChannelRoute ? (paramUsername?.trim() || '') : '';

  const tab = useMemo(() => {
    const raw = searchParams.get('tab');
    const defaultTab = isChannelRoute ? 'twitch' : 'wall';
    let t = raw ? raw.toLowerCase() : defaultTab;
    if (t === 'followers' || t === 'following') t = 'friendships';
    if (t === 'channel') t = 'twitch';
    return TAB_KEYS.includes(t) ? t : defaultTab;
  }, [searchParams, isChannelRoute]);

  const setTab = (next) => {
    const p = new URLSearchParams(searchParams);
    if (isChannelRoute) {
      if (next === 'twitch') p.delete('tab');
      else p.set('tab', next);
    } else if (next === 'wall') {
      p.delete('tab');
    } else {
      p.set('tab', next);
    }
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

  const loadChannelRoute = useCallback(() => {
    if (!channelRouteLogin) return;
    setLoading(true);
    setErr('');
    channelsApi
      .profile(channelRouteLogin)
      .then((cp) => {
        const siteUser = cp?.clipstream_site?.username;
        if (siteUser) {
          navigate(`/u/${encodeURIComponent(siteUser)}?tab=twitch`, { replace: true });
          return;
        }
        setData(buildGuestProfileFromChannel(cp, channelRouteLogin));
      })
      .catch((e) => setErr(e.message || 'Не удалось загрузить канал'))
      .finally(() => setLoading(false));
  }, [channelRouteLogin, navigate]);

  useEffect(() => {
    if (isChannelRoute) loadChannelRoute();
    else if (targetUsername) load();
  }, [isChannelRoute, targetUsername, load, loadChannelRoute]);

  const isSelf = isAuth && user && data && data.id != null && user.id === data.id;
  const isGuestChannel = !!data?.is_channel_guest;

  const visibleTabs = useMemo(() => {
    if (isGuestChannel) {
      return [
        { id: 'overview', label: 'Обзор' },
        { id: 'twitch', label: 'Канал Twitch' },
      ];
    }
    const base = [
      { id: 'overview', label: 'Обзор' },
      { id: 'friendships', label: 'Друзья' },
      { id: 'wall', label: 'Лента' },
      { id: 'clips', label: 'Клипы' },
    ];
    if (data?.twitch_login) {
      base.push({ id: 'twitch', label: 'Канал Twitch' });
    }
    if (isSelf) {
      base.push(
        { id: 'activity', label: 'Активность' },
        { id: 'friends_feed', label: 'Друзья смотрят' },
        { id: 'settings', label: 'Настройки' },
      );
    }
    return base;
  }, [isSelf, data?.twitch_login, isGuestChannel]);

  const sidebarNav = visibleTabs.map(({ id, label }) => ({
    id,
    label,
    icon: TAB_ICONS[id],
    active: tab === id,
    onClick: () => setTab(id),
  }));

  useEffect(() => {
    if (isGuestChannel) {
      if (!['overview', 'twitch'].includes(tab)) setTab('twitch');
      return;
    }
    if (!data?.id) return;
    if (!isSelf && ['activity', 'friends_feed', 'settings'].includes(tab)) setTab('wall');
    if (tab === 'twitch' && !data.twitch_login) setTab('wall');
  }, [tab, isSelf, data?.id, data?.twitch_login, isGuestChannel]);

  useEffect(() => {
    if (isGuestChannel || !data?.id) return;
    let cancelled = false;
    const run = async () => {
      setTabLoading(true);
      try {
        if (tab === 'friendships') {
          const [fr, inc, out] = await Promise.all([
            socialApi.friends(data.id, 'limit=80&offset=0'),
            isSelf ? socialApi.friendRequestsIncoming('limit=40&offset=0') : Promise.resolve({ users: [] }),
            isSelf ? socialApi.friendRequestsOutgoing('limit=40&offset=0') : Promise.resolve({ users: [] }),
          ]);
          if (!cancelled) {
            setFriendList(fr.users || []);
            setIncomingFriends(inc.users || []);
            setOutgoingFriends(out.users || []);
          }
        } else if (tab === 'wall') {
          const r = await socialApi.wall(data.username, 'limit=30&offset=0');
          if (!cancelled) setWallPosts(r.posts || []);
        } else if (tab === 'clips') {
          const r = await socialApi.userClips(data.id, 'limit=24&offset=0');
          if (!cancelled) setAuthorClips(r.clips || []);
        } else if (tab === 'activity' && isSelf) {
          const r = await socialApi.watchActivity(data.id, 'limit=40&offset=0');
          if (!cancelled) setWatchActivity(r.activity || []);
        } else if (tab === 'friends_feed' && isSelf) {
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
        if (!cancelled && tab === 'friendships') {
          setFriendList([]);
          setIncomingFriends([]);
          setOutgoingFriends([]);
        }
        if (!cancelled && tab === 'wall') setWallPosts([]);
        if (!cancelled && tab === 'clips') setAuthorClips([]);
        if (!cancelled && tab === 'activity') setWatchActivity([]);
        if (!cancelled && tab === 'friends_feed') setFriendsClips([]);
      } finally {
        if (!cancelled) setTabLoading(false);
      }
    };
    if (['friendships', 'wall', 'clips', 'activity', 'friends_feed', 'settings'].includes(tab)) {
      run();
    }
    return () => {
      cancelled = true;
    };
  }, [tab, data?.id, data?.username, isSelf, clipLangKey, isGuestChannel]);

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

  const reloadFriendshipsTab = useCallback(async () => {
    if (!data?.id) return;
    try {
      const [fr, inc, out] = await Promise.all([
        socialApi.friends(data.id, 'limit=80&offset=0'),
        isSelf ? socialApi.friendRequestsIncoming('limit=40&offset=0') : Promise.resolve({ users: [] }),
        isSelf ? socialApi.friendRequestsOutgoing('limit=40&offset=0') : Promise.resolve({ users: [] }),
      ]);
      setFriendList(fr.users || []);
      setIncomingFriends(inc.users || []);
      setOutgoingFriends(out.users || []);
    } catch {
      /* ignore */
    }
  }, [data?.id, isSelf]);

  const runFriendAction = async (action, targetUserId) => {
    if (!isAuth) return;
    const peerId = targetUserId !== undefined && targetUserId !== null ? targetUserId : data?.id;
    if (!peerId) return;
    if (targetUserId === undefined && isSelf) return;
    setFriendBusy(true);
    try {
      if (action === 'request') await socialApi.friendRequest(peerId);
      else if (action === 'cancel') await socialApi.cancelFriendRequest(peerId);
      else if (action === 'accept') await socialApi.acceptFriend(peerId);
      else if (action === 'decline') await socialApi.declineFriend(peerId);
      else if (action === 'remove') await socialApi.removeFriend(peerId);
      await load();
      if (tab === 'friendships') await reloadFriendshipsTab();
      if (isSelf) await fetchMe();
    } catch (e) {
      setErr(e.message || 'Ошибка');
    } finally {
      setFriendBusy(false);
    }
  };

  const submitWall = async () => {
    const t = wallText.trim();
    if (!isSelf || !data?.username || !t) return;
    setWallBusy(true);
    try {
      await socialApi.wallPost(data.username, t);
      setWallText('');
      const r = await socialApi.wall(data.username, 'limit=30&offset=0');
      setWallPosts(r.posts || []);
      await load();
      await fetchMe();
    } catch (e) {
      setErr(e.message || 'Не удалось опубликовать');
    } finally {
      setWallBusy(false);
    }
  };

  const copyProfileUrl = () => {
    if (!data) return;
    const url = isGuestChannel
      ? `${window.location.origin}/channel/${encodeURIComponent(data.channel_route_login)}`
      : `${window.location.origin}/u/${encodeURIComponent(targetUsername)}`;
    navigator.clipboard.writeText(url).catch(() => {});
  };

  const goClip = (clip) => navigate(`/clip/${clip.id}`);

  if (!isChannelRoute && !targetUsername) {
    return (
      <div className="p-8 max-w-lg mx-auto text-center">
        <p className="text-gray-400 text-sm mb-4">
          Неверный адрес. Используйте <span className="text-brand-400">/u/ник</span> или{' '}
          <span className="text-brand-400">/channel/логин</span>
        </p>
        <Link to="/" className="text-brand-400 text-sm hover:underline">На главную</Link>
      </div>
    );
  }

  if (isChannelRoute && !channelRouteLogin) {
    return (
      <div className="p-8 max-w-lg mx-auto text-center">
        <p className="text-gray-400 text-sm mb-4">Не указан канал Twitch.</p>
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
  const friendship = data.friendship || 'none';
  const earnedAchievements = Array.isArray(data.achievements_earned) ? data.achievements_earned : [];
  const twCh = data.twitch_channel?.twitch;
  const twLive = !!(twCh?.is_live && twCh?.stream);
  const userCoverUrl =
    twCh?.offline_image_url ||
    (twCh?.is_live && twCh?.stream?.thumbnail_url ? twCh.stream.thumbnail_url : '') ||
    '';
  const twitchFeedLogin = (isGuestChannel ? data.channel_route_login : data.twitch_login) || '';

  return (
    <SocialProfileLayout
      flavor="user"
      coverUrl={userCoverUrl || undefined}
      avatarUrl={avatarUrl(data)}
      avatarBadge={
        twLive ? (
          <span className="absolute bottom-1 right-1 flex items-center gap-0.5 text-[9px] font-bold uppercase tracking-wide px-1.5 py-0.5 rounded-md bg-accent-red text-white shadow-md border border-[#0c0712]">
            <Radio className="w-2.5 h-2.5" />
            Live
          </span>
        ) : null
      }
      title={data.username}
      titleBadges={
        <>
          {isSelf && (user?.role ?? 0) === 2 && (
            <span className="text-[10px] font-bold uppercase tracking-wide px-2 py-0.5 rounded-md border border-red-500/45 bg-red-500/12 text-red-200">Админ</span>
          )}
          {isSelf && (user?.role ?? 0) === 1 && (
            <span className="text-[10px] font-bold uppercase tracking-wide px-2 py-0.5 rounded-md border border-amber-500/45 bg-amber-500/12 text-amber-100">Модератор</span>
          )}
          {isSelf && (user?.role ?? 0) === 3 && (
            <span className="text-[10px] font-bold uppercase tracking-wide px-2 py-0.5 rounded-md border border-fuchsia-500/45 bg-fuchsia-500/12 text-fuchsia-100">VIP</span>
          )}
        </>
      }
      subtitle={
        isGuestChannel ? (
          <span className="text-gray-400">@{data.twitch_login}</span>
        ) : (
          <>
            <span
              className="inline-flex items-center gap-1.5"
              title={
                isSelf
                  ? 'По вашим клипам: лайк +2 / снятие −2, дизлайк −1 / снятие +1; корневой комментарий +3 (удаление −3). Без учёта самолайков и своих комментариев.'
                  : 'За реакции под клипами этого пользователя на сайте и за корневые комментарии к ним.'
              }
            >
              Репутация{' '}
              <span className="text-brand-300 font-semibold tabular-nums">{data.reputation ?? 0}</span>
            </span>
            <span className="text-gray-600 mx-2">·</span>
            <span className="text-gray-300">{BADGE_LABELS[data.badge] ?? '—'}</span>
          </>
        )
      }
      tagRow={
        <>
          {data.twitch_login ? (
            <button type="button" onClick={() => setTab('twitch')} className="inline-flex items-center gap-1.5 text-[11px] font-medium px-2.5 py-1 rounded-lg bg-[#6441a4]/20 text-[#e0d4ff] border border-[#7d5bbe]/40 hover:bg-[#6441a4]/30 transition-colors">
              Twitch @{data.twitch_login}
              {twLive ? <span className="w-1.5 h-1.5 rounded-full bg-accent-red animate-pulse" /> : null}
            </button>
          ) : !isGuestChannel ? (
            data.twitch_id ? (
              <span className="inline-flex items-center gap-1.5 text-[11px] text-gray-500 px-2.5 py-1 rounded-lg border border-brand-800">Twitch привязан</span>
            ) : (
              <span className="inline-flex items-center gap-1.5 text-[11px] text-gray-500 px-2.5 py-1 rounded-lg border border-brand-800/80">Без канала Twitch</span>
            )
          ) : null}
          {isSelf && !isGuestChannel && data.used_clipstream_via_twitch === true && (
            <span className="inline-flex items-center gap-1.5 text-[11px] font-medium px-2.5 py-1 rounded-lg bg-sky-500/10 text-sky-200/95 border border-sky-500/25">
              <LogIn className="w-3.5 h-3.5 opacity-90" />
              Вход через Twitch
            </span>
          )}
          {isSelf && !isGuestChannel && data.used_clipstream_via_twitch === false && (
            <span className="inline-flex items-center gap-1.5 text-[11px] text-gray-500 px-2.5 py-1 rounded-lg border border-brand-800/80">Twitch не привязан к входу</span>
          )}
          {!isGuestChannel && data.on_site_now === true && (
            <span className="inline-flex items-center gap-1.5 text-[11px] font-semibold px-2.5 py-1 rounded-lg bg-emerald-500/15 text-emerald-200 border border-emerald-500/30">
              <Circle className="w-2 h-2 fill-emerald-400 text-emerald-400 shrink-0" />
              {isSelf ? 'Вы сейчас на сайте' : 'Сейчас на сайте'}
            </span>
          )}
        </>
      }
      actionRow={
        <div className="flex flex-col gap-3 w-full min-w-0">
          <div className="flex flex-wrap gap-2">
            <button type="button" onClick={copyProfileUrl} className="inline-flex items-center gap-1.5 text-xs px-3 py-2 rounded-xl bg-brand-800/90 border border-brand-600/40 text-gray-200 hover:bg-brand-700 hover:text-white transition-colors">
              <Share2 className="w-3.5 h-3.5 shrink-0" />
              Скопировать ссылку
            </button>
            {!isGuestChannel && (
              <>
                <button type="button" onClick={() => setTab('wall')} className="inline-flex items-center gap-1.5 text-xs px-3 py-2 rounded-xl bg-brand-800/50 border border-brand-700/50 text-gray-300 hover:bg-brand-800 hover:text-white transition-colors">
                  <Rss className="w-3.5 h-3.5 shrink-0" />
                  Лента
                </button>
                <button type="button" onClick={() => setTab('clips')} className="inline-flex items-center gap-1.5 text-xs px-3 py-2 rounded-xl bg-brand-800/50 border border-brand-700/50 text-gray-300 hover:bg-brand-800 hover:text-white transition-colors">
                  <Clapperboard className="w-3.5 h-3.5 shrink-0" />
                  Клипы
                </button>
              </>
            )}
            {data.twitch_login ? (
              <button type="button" onClick={() => setTab('twitch')} className="inline-flex items-center gap-1.5 text-xs px-3 py-2 rounded-xl bg-[#392e5c]/80 border border-[#5c4d8a]/50 text-[#e6e0ff] hover:bg-[#4a3d73] transition-colors">
                <ExternalLink className="w-3.5 h-3.5 shrink-0" />
                Эфир и каталог Twitch
              </button>
            ) : null}
            {isGuestChannel ? (
              <button
                type="button"
                onClick={() => navigate(-1)}
                className="inline-flex items-center gap-1.5 text-xs px-3 py-2 rounded-xl bg-brand-800 border border-brand-700 text-gray-200 hover:bg-brand-700 hover:text-white transition-colors"
              >
                Назад
              </button>
            ) : null}
            {isSelf && isAdmin && (
              <Link to="/admin" className="inline-flex items-center gap-2 px-3 py-2 rounded-xl bg-red-500/15 border border-red-500/35 text-red-200 text-xs font-semibold hover:bg-red-500/25">
                <Crown className="w-4 h-4" /> Админ
              </Link>
            )}
            {isSelf && staffAccess && (
              <Link to="/staff" className="inline-flex items-center gap-2 px-3 py-2 rounded-xl bg-amber-500/15 border border-amber-500/35 text-amber-200 text-xs font-semibold hover:bg-amber-500/25">
                <Wrench className="w-4 h-4" /> Консоль
              </Link>
            )}
          </div>
          {isSelf && !isAdmin && !staffAccess ? (
            <p className="text-xs text-gray-500">Ваш публичный профиль на ClipStream</p>
          ) : null}
          {!isGuestChannel && !isAuth && !isSelf && (
            <p className="text-xs text-gray-500 leading-relaxed">Войдите через меню в шапке, чтобы добавлять в друзья.</p>
          )}
          <div className="flex flex-wrap gap-2">
            {!isGuestChannel && isAuth && !isSelf && friendship === 'none' && (
              <button type="button" disabled={friendBusy} onClick={() => runFriendAction('request')} className="inline-flex justify-center items-center gap-2 px-4 py-2 rounded-xl text-sm font-semibold bg-brand-500 text-white hover:bg-brand-400 disabled:opacity-50 shadow-lg shadow-brand-900/40">
                {friendBusy ? <Loader2 className="w-4 h-4 animate-spin" /> : <UserPlus className="w-4 h-4" />}
                В друзья
              </button>
            )}
            {isAuth && !isSelf && friendship === 'outgoing' && (
              <button type="button" disabled={friendBusy} onClick={() => runFriendAction('cancel')} className="inline-flex justify-center items-center gap-2 px-4 py-2 rounded-xl text-sm font-semibold bg-brand-800 border border-amber-500/40 text-amber-200 hover:bg-brand-700 disabled:opacity-50">
                {friendBusy ? <Loader2 className="w-4 h-4 animate-spin" /> : <X className="w-4 h-4" />}
                Отменить заявку
              </button>
            )}
            {!isGuestChannel && isAuth && !isSelf && friendship === 'incoming' && (
              <>
                <button type="button" disabled={friendBusy} onClick={() => runFriendAction('accept')} className="inline-flex justify-center items-center gap-2 px-4 py-2 rounded-xl text-sm font-semibold bg-emerald-600/90 text-white hover:bg-emerald-500 disabled:opacity-50">
                  {friendBusy ? <Loader2 className="w-4 h-4 animate-spin" /> : <Check className="w-4 h-4" />}
                  Принять
                </button>
                <button type="button" disabled={friendBusy} onClick={() => runFriendAction('decline')} className="inline-flex justify-center items-center gap-2 px-4 py-2 rounded-xl text-sm font-semibold bg-brand-800 border border-brand-600 text-gray-200 hover:bg-brand-700 disabled:opacity-50">
                  <X className="w-4 h-4" />
                  Отклонить
                </button>
              </>
            )}
            {!isGuestChannel && isAuth && !isSelf && friendship === 'friends' && (
              <button type="button" disabled={friendBusy} onClick={() => runFriendAction('remove')} className="inline-flex justify-center items-center gap-2 px-4 py-2 rounded-xl text-sm font-semibold bg-brand-800 border border-brand-600 text-gray-200 hover:bg-red-900/30 hover:border-red-500/40 hover:text-red-200 disabled:opacity-50">
                {friendBusy ? <Loader2 className="w-4 h-4 animate-spin" /> : <UserMinus className="w-4 h-4" />}
                Удалить из друзей
              </button>
            )}
          </div>
        </div>
      }
      sidebarNav={sidebarNav}
      sidebarFooter={
        isGuestChannel ? (
          <div className="space-y-1.5 text-gray-400">
            <p className="text-[10px] font-bold uppercase tracking-[0.12em] text-gray-500">Канал</p>
            <p>
              <span className="text-gray-500">Клипов на сайте:</span>{' '}
              <span className="text-white font-semibold tabular-nums">{stats.clips_created_count ?? 0}</span>
            </p>
            {twCh?.view_count != null ? (
              <p>
                <span className="text-gray-500">Просмотры Twitch:</span>{' '}
                <span className="text-white font-semibold tabular-nums">{twCh.view_count}</span>
              </p>
            ) : null}
            {!data.channel_api_found ? (
              <p className="text-[10px] text-amber-400/90">Данные канала Twitch сейчас недоступны; клипы по ссылке на канал.</p>
            ) : null}
          </div>
        ) : (
          <div className="space-y-2">
            <p className="text-[10px] font-bold uppercase tracking-[0.12em] text-gray-500">Сводка</p>
            <div className="grid grid-cols-2 gap-2">
              <button type="button" onClick={() => setTab('friendships')} className="rounded-lg bg-brand-950/70 px-2 py-2 border border-brand-700/40 text-left hover:border-brand-500/45 transition-colors">
                <Users className="w-3.5 h-3.5 text-sky-400 mb-0.5" />
                <p className="text-lg font-bold text-white tabular-nums leading-tight">{stats.friends_count ?? 0}</p>
                <p className="text-[10px] text-gray-500">Друзей</p>
              </button>
              <button type="button" onClick={() => setTab('wall')} className="rounded-lg bg-brand-950/70 px-2 py-2 border border-brand-700/40 text-left hover:border-fuchsia-500/35 transition-colors">
                <Rss className="w-3.5 h-3.5 text-fuchsia-400 mb-0.5" />
                <p className="text-lg font-bold text-white tabular-nums leading-tight">{stats.wall_posts_count ?? 0}</p>
                <p className="text-[10px] text-gray-500">В ленте</p>
              </button>
              <button type="button" onClick={() => setTab('overview')} className="rounded-lg bg-brand-950/70 px-2 py-2 border border-brand-700/40 text-left hover:border-emerald-500/35 transition-colors">
                <MessageSquare className="w-3.5 h-3.5 text-emerald-400 mb-0.5" />
                <p className="text-lg font-bold text-white tabular-nums leading-tight">{stats.comments_count ?? 0}</p>
                <p className="text-[10px] text-gray-500">Коммент.</p>
              </button>
              <button type="button" onClick={() => setTab('clips')} className="rounded-lg bg-brand-950/70 px-2 py-2 border border-violet-800/40 text-left hover:border-violet-500/45 transition-colors">
                <Clapperboard className="w-3.5 h-3.5 text-violet-400 mb-0.5" />
                <p className="text-lg font-bold text-white tabular-nums leading-tight">{stats.clips_created_count ?? 0}</p>
                <p className="text-[10px] text-gray-500">Клипов</p>
              </button>
            </div>
          </div>
        )
      }
    >

      {isSelf && (user?.account_blocked || user?.chat_banned_until) && (
        <div className="mb-6 rounded-xl border border-amber-500/40 bg-amber-950/30 px-4 py-3 text-sm text-amber-100">
          {user?.account_blocked && <p>Аккаунт заблокирован модерацией.</p>}
          {!user?.account_blocked && user?.chat_banned_until && (
            <p>Ограничение чата до {user.chat_banned_until}</p>
          )}
        </div>
      )}

      {tab === 'overview' && (
        <div className="space-y-5 sm:space-y-6">
          <p className="text-sm text-gray-400 max-w-[min(100%,42rem)] text-pretty">
            {isGuestChannel
              ? 'Клипы и карточка канала Twitch.'
              : isSelf
                ? 'Ваша активность на сайте, привязанный Twitch и переход к эфиру и клипам.'
                : 'Клипы, лента и друзья на странице пользователя.'}
          </p>

          {data.twitch_login ? (
            <div className="rounded-2xl border border-[#5c4d8a]/45 bg-gradient-to-r from-[#1f1633]/90 to-brand-900/50 p-4 sm:p-5 flex flex-col sm:flex-row sm:items-center sm:justify-between gap-4">
              <div className="min-w-0">
                <p className="text-[10px] font-bold uppercase tracking-[0.14em] text-[#c4b5fd] mb-1">Twitch</p>
                <p className="text-white font-semibold truncate">
                  {twCh?.display_name || data.twitch_login}
                  <span className="text-gray-500 font-normal ml-2">@{data.twitch_login}</span>
                </p>
                <p className="text-xs text-gray-500 mt-1">
                  Эфир, описание канала и все клипы с Twitch — во вкладке «Канал Twitch».
                </p>
              </div>
              <div className="flex flex-wrap gap-2 shrink-0">
                <button
                  type="button"
                  onClick={() => setTab('twitch')}
                  className="inline-flex items-center justify-center gap-2 px-4 py-2.5 rounded-xl bg-[#6441a4] hover:bg-[#7552b3] text-white text-sm font-semibold transition-colors"
                >
                  <Radio className="w-4 h-4" />
                  Канал и клипы
                </button>
                <a
                  href={`https://www.twitch.tv/${encodeURIComponent(data.twitch_login)}`}
                  target="_blank"
                  rel="noopener noreferrer"
                  className="inline-flex items-center justify-center gap-2 px-4 py-2.5 rounded-xl border border-[#7d5bbe]/50 text-[#e8e0ff] text-sm font-medium hover:bg-[#392e5c]/60 transition-colors"
                >
                  <ExternalLink className="w-4 h-4" />
                  Открыть Twitch
                </a>
              </div>
            </div>
          ) : null}

          {!isGuestChannel && (
          <>
          <ProfileSection
            eyebrow={isSelf ? 'Ваш аккаунт' : 'На сайте'}
            title="Активность"
            description={
              isSelf
                ? 'Просмотры, лайки, комментарии и клипы с Twitch на сайте.'
                : 'Публичная статистика активности на сайте.'
            }
          >
            <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 xl:grid-cols-4 2xl:grid-cols-6 gap-3 sm:gap-4 lg:gap-5">
              <div className="rounded-xl p-4 border border-violet-800/40 bg-brand-950/40 hover:border-violet-500/35 transition-colors">
                <Clapperboard className="w-5 h-5 text-violet-400 mb-2" />
                <p className="text-2xl font-bold text-white tabular-nums">{stats.clips_created_count ?? 0}</p>
                <p className="text-xs text-gray-500 mt-1 leading-snug">Клипов с Twitch на сайте</p>
              </div>
              <div className="rounded-xl p-4 border border-brand-700/45 bg-brand-950/40">
                <Heart className="w-5 h-5 text-rose-400 mb-2" />
                <p className="text-2xl font-bold text-white tabular-nums">{stats.liked_count || 0}</p>
                <p className="text-xs text-gray-500 mt-1 leading-snug">Лайков поставлено</p>
              </div>
              <div className="rounded-xl p-4 border border-brand-700/45 bg-brand-950/40">
                <Clock className="w-5 h-5 text-amber-400/90 mb-2" />
                <p className="text-2xl font-bold text-white tabular-nums">{stats.unique_viewed_clips || 0}</p>
                <p className="text-xs text-gray-500 mt-1 leading-snug">Уникальных клипов просмотрено</p>
              </div>
              <button
                type="button"
                onClick={() => setTab('wall')}
                className="rounded-xl p-4 border border-brand-700/45 bg-brand-950/40 text-left hover:border-emerald-500/40 transition-colors w-full"
              >
                <MessageSquare className="w-5 h-5 text-emerald-400 mb-2" />
                <p className="text-2xl font-bold text-white tabular-nums">{stats.comments_count || 0}</p>
                <p className="text-xs text-gray-500 mt-1 leading-snug">Комментариев · перейти к ленте</p>
              </button>
              <div className="rounded-xl p-4 border border-brand-700/45 bg-brand-950/40 sm:col-span-2 lg:col-span-1 xl:col-span-2 2xl:col-span-1">
                <Star className="w-5 h-5 text-sky-400 mb-2" />
                <p className="text-base font-bold text-white truncate">{stats.favorite_game_name || '—'}</p>
                <p className="text-xs text-gray-500 mt-1 leading-snug">Частая категория</p>
              </div>
              <div className="rounded-xl p-4 border border-brand-700/45 bg-brand-950/40">
                <MoonStar className="w-5 h-5 text-indigo-400 mb-2" />
                <p className="text-2xl font-bold text-white tabular-nums">{stats.night_views || 0}</p>
                <p className="text-xs text-gray-500 mt-1 leading-snug">Ночных просмотров</p>
              </div>
            </div>
          </ProfileSection>

          <ProfileSection eyebrow="Награды" title="Полученные достижения" description="Уникальные значки за действия на платформе.">
            {earnedAchievements.length === 0 ? (
              <p className="text-sm text-gray-500">Пока нет зафиксированных достижений.</p>
            ) : (
              <ul className="grid gap-2 sm:grid-cols-2 lg:grid-cols-3 2xl:grid-cols-4">
                {earnedAchievements.map((row, idx) => (
                  <li
                    key={`${row.key}-${idx}`}
                    className="flex items-center justify-between gap-3 rounded-xl border border-brand-600/35 bg-brand-950/50 px-4 py-3"
                  >
                    <span className="text-white font-medium flex items-center gap-2">
                      <Sparkles className="w-4 h-4 text-fuchsia-400 shrink-0" />
                      {ACHIEVEMENT_TITLES[row.key] || row.key}
                    </span>
                    <span className="text-[11px] text-gray-500 shrink-0">{row.earned_at}</span>
                  </li>
                ))}
              </ul>
            )}
          </ProfileSection>

          {achievements.length > 0 ? (
            <ProfileSection
              eyebrow="Только вы"
              title="Прогресс до следующих достижений"
              description="Не видно другим пользователям."
            >
              <div className="grid grid-cols-1 md:grid-cols-2 gap-3">
                {achievements.map((b) => {
                  const pct = b.target > 0 ? Math.min(100, Math.round((b.progress / b.target) * 100)) : 0;
                  return (
                    <div
                      key={b.id}
                      className={`rounded-xl border p-4 ${b.unlocked ? 'border-brand-500/60 bg-brand-800/25' : 'border-brand-700/50 bg-brand-950/40'}`}
                    >
                      <div className="flex items-center justify-between gap-2 mb-1.5">
                        <p className="text-sm font-semibold text-white">{b.title}</p>
                        <span
                          className={`text-[10px] px-2 py-0.5 rounded-full ${b.unlocked ? 'bg-emerald-500/20 text-emerald-300' : 'bg-brand-800 text-gray-400'}`}
                        >
                          {b.unlocked ? 'Получен' : 'В процессе'}
                        </span>
                      </div>
                      <p className="text-xs text-gray-500 mb-2">{b.description}</p>
                      <div className="w-full h-1.5 rounded-full bg-brand-950 overflow-hidden">
                        <div className="h-full bg-gradient-to-r from-brand-500 to-fuchsia-500 rounded-full" style={{ width: `${pct}%` }} />
                      </div>
                    </div>
                  );
                })}
              </div>
            </ProfileSection>
          ) : null}

          <ProfileSection eyebrow="Ранг" title="История бейджей" description="Изменения уровня репутации на платформе.">
            {!data.badge_history || data.badge_history.length === 0 ? (
              <p className="text-sm text-gray-500">Пока нет записей.</p>
            ) : (
              <ul className="space-y-2 text-sm">
                {data.badge_history.map((row) => (
                  <li
                    key={row.id}
                    className="flex justify-between gap-4 border-b border-brand-800/90 pb-2.5 last:border-0 last:pb-0"
                  >
                    <span className="text-gray-300">
                      <span className="text-white font-medium">{row.badge}</span>{' '}
                      <span className={row.event_type === 'granted' ? 'text-emerald-400' : 'text-amber-400'}>
                        ({row.event_type})
                      </span>
                    </span>
                    <span className="text-gray-500 shrink-0 text-xs">{row.created_at}</span>
                  </li>
                ))}
              </ul>
            )}
          </ProfileSection>
          </>
          )}
        </div>
      )}

      {tab === 'friendships' && (
        <div className="space-y-8">
          <p className="text-sm text-gray-500 max-w-[min(100%,40rem)] text-pretty">
            {isSelf
              ? 'Друзья и заявки на сайте — это не подписки Twitch.'
              : 'Друзья на сайте, не подписки на Twitch.'}
          </p>
          {tabLoading ? (
            <div className="flex justify-center py-12 text-gray-500">
              <Loader2 className="w-8 h-8 animate-spin" />
            </div>
          ) : (
            <>
              {isSelf && (
                <>
                  <section>
                    <h3 className="text-white font-semibold flex items-center gap-2 mb-3">
                      <UserPlus className="w-4 h-4 text-brand-400" />
                      Входящие заявки
                    </h3>
                    {incomingFriends.length === 0 ? (
                      <p className="text-sm text-gray-500">Нет новых заявок.</p>
                    ) : (
                      <ul className="space-y-2">
                        {incomingFriends.map((u) => (
                          <li
                            key={u.id}
                            className="flex flex-wrap items-center justify-between gap-3 p-3 rounded-xl border border-brand-700/50 bg-brand-800/40"
                          >
                            <Link
                              to={`/u/${encodeURIComponent(u.username)}`}
                              className="flex items-center gap-3 min-w-0"
                            >
                              <img
                                src={avatarUrl(u)}
                                alt=""
                                className="w-10 h-10 rounded-full object-cover bg-brand-900 shrink-0"
                              />
                              <div className="min-w-0">
                                <p className="text-white font-medium truncate">{u.username}</p>
                                {u.since && <p className="text-[11px] text-gray-500">{u.since}</p>}
                              </div>
                            </Link>
                            <div className="flex flex-wrap gap-2">
                              <button
                                type="button"
                                disabled={friendBusy}
                                onClick={() => runFriendAction('accept', u.id)}
                                className="inline-flex items-center gap-1 px-3 py-1.5 rounded-lg bg-emerald-600/90 text-white text-xs font-semibold disabled:opacity-40"
                              >
                                <Check className="w-3.5 h-3.5" />
                                Принять
                              </button>
                              <button
                                type="button"
                                disabled={friendBusy}
                                onClick={() => runFriendAction('decline', u.id)}
                                className="inline-flex items-center gap-1 px-3 py-1.5 rounded-lg border border-brand-600 text-gray-300 text-xs hover:bg-brand-800 disabled:opacity-40"
                              >
                                <X className="w-3.5 h-3.5" />
                                Отклонить
                              </button>
                            </div>
                          </li>
                        ))}
                      </ul>
                    )}
                  </section>
                  <section>
                    <h3 className="text-white font-semibold mb-3">Исходящие заявки</h3>
                    {outgoingFriends.length === 0 ? (
                      <p className="text-sm text-gray-500">Нет исходящих заявок.</p>
                    ) : (
                      <ul className="space-y-2">
                        {outgoingFriends.map((u) => (
                          <li
                            key={u.id}
                            className="flex flex-wrap items-center justify-between gap-3 p-3 rounded-xl border border-brand-700/50 bg-brand-800/40"
                          >
                            <Link
                              to={`/u/${encodeURIComponent(u.username)}`}
                              className="flex items-center gap-3 min-w-0"
                            >
                              <img
                                src={avatarUrl(u)}
                                alt=""
                                className="w-10 h-10 rounded-full object-cover bg-brand-900 shrink-0"
                              />
                              <div className="min-w-0">
                                <p className="text-white font-medium truncate">{u.username}</p>
                                {u.since && <p className="text-[11px] text-gray-500">{u.since}</p>}
                              </div>
                            </Link>
                            <button
                              type="button"
                              disabled={friendBusy}
                              onClick={() => runFriendAction('cancel', u.id)}
                              className="inline-flex items-center gap-1 px-3 py-1.5 rounded-lg border border-amber-500/40 text-amber-200 text-xs font-semibold hover:bg-amber-500/10 disabled:opacity-40"
                            >
                              <X className="w-3.5 h-3.5" />
                              Отменить
                            </button>
                          </li>
                        ))}
                      </ul>
                    )}
                  </section>
                </>
              )}
              <section>
                <h3 className="text-white font-semibold flex items-center gap-2 mb-3">
                  <Users className="w-4 h-4 text-sky-400" />
                  {isSelf ? 'Ваши друзья' : `Друзья — ${data.username}`}
                </h3>
                {friendList.length === 0 ? (
                  <p className="text-sm text-gray-500">Пока нет друзей на сайте.</p>
                ) : (
                  <ul className="grid gap-2 sm:grid-cols-2 lg:grid-cols-3 xl:grid-cols-4">
                    {friendList.map((u) => (
                      <li
                        key={u.id}
                        className="flex items-center justify-between gap-2 p-3 rounded-xl border border-brand-700/40 bg-brand-800/30"
                      >
                        <Link
                          to={`/u/${encodeURIComponent(u.username)}`}
                          className="flex items-center gap-3 min-w-0 flex-1"
                        >
                          <img
                            src={avatarUrl(u)}
                            alt=""
                            className="w-11 h-11 rounded-full object-cover bg-brand-900 shrink-0"
                          />
                          <div className="min-w-0">
                            <p className="text-white font-medium truncate">{u.username}</p>
                            {u.since && <p className="text-[11px] text-gray-500">с {u.since}</p>}
                          </div>
                        </Link>
                        {isSelf ? (
                          <button
                            type="button"
                            disabled={friendBusy}
                            onClick={() => runFriendAction('remove', u.id)}
                            className="shrink-0 text-[11px] px-2 py-1 rounded-lg border border-brand-600 text-gray-400 hover:text-red-300 hover:border-red-500/40 disabled:opacity-40"
                          >
                            Убрать
                          </button>
                        ) : null}
                      </li>
                    ))}
                  </ul>
                )}
              </section>
            </>
          )}
        </div>
      )}

      {tab === 'wall' && (
        <div>
          <p className="text-sm text-gray-500 mb-4 max-w-[min(100%,40rem)] text-pretty">
            {isSelf ? (
              <>
                Лента профиля. Упоминания: <code className="text-brand-400 text-xs">@ник</code> и{' '}
                <code className="text-brand-400 text-xs">@clip/ID</code>.
              </>
            ) : (
              'Записи в ленте этого профиля.'
            )}
          </p>
          {isSelf && (
            <div className="mb-6 rounded-xl border border-brand-700/40 bg-[#12101a]/95 shadow-md shadow-black/15 p-4 sm:p-5">
              <textarea
                value={wallText}
                onChange={(e) => setWallText(e.target.value)}
                maxLength={4000}
                rows={4}
                placeholder="Новая запись в ленте профиля…"
                className="w-full bg-brand-900 border border-brand-700 rounded-xl px-3 py-2 text-sm text-white resize-y min-h-[100px] placeholder:text-gray-600"
              />
              <button
                type="button"
                disabled={wallBusy || !wallText.trim()}
                onClick={submitWall}
                className="mt-3 inline-flex items-center gap-2 px-4 py-2 rounded-lg bg-brand-500 text-white text-sm font-semibold hover:bg-brand-400 disabled:opacity-40"
              >
                {wallBusy ? <Loader2 className="w-4 h-4 animate-spin" /> : null}
                Опубликовать
              </button>
            </div>
          )}
          {tabLoading ? (
            <div className="flex justify-center py-12">
              <Loader2 className="w-8 h-8 animate-spin text-gray-500" />
            </div>
          ) : wallPosts.length === 0 ? (
            <p className="text-center text-gray-500 text-sm py-12">В ленте пока нет записей.</p>
          ) : (
            <ul className="space-y-4">
              {wallPosts.map((post) => (
                <li
                  key={post.id}
                  className="rounded-xl border border-brand-700/40 bg-[#12101a]/95 shadow-md shadow-black/20 p-4 hover:border-brand-600/45 transition-colors"
                >
                  <div className="flex gap-3">
                    <Link to={`/u/${encodeURIComponent(post.author.username)}`} className="shrink-0">
                      <img
                        src={avatarUrl(post.author)}
                        alt=""
                        className="w-11 h-11 rounded-full object-cover bg-brand-900"
                      />
                    </Link>
                    <div className="min-w-0 flex-1">
                      <div className="flex flex-wrap items-baseline gap-2">
                        <Link
                          to={`/u/${encodeURIComponent(post.author.username)}`}
                          className="text-white font-semibold text-sm hover:text-brand-300"
                        >
                          {post.author.username}
                        </Link>
                        <span className="text-[11px] text-gray-500">{post.created_at}</span>
                      </div>
                      <div className="mt-2 text-sm text-gray-200">
                        <WallPostBody body={post.body} mentions={post.mentions} />
                      </div>
                    </div>
                  </div>
                </li>
              ))}
            </ul>
          )}
        </div>
      )}

      {tab === 'twitch' && twitchFeedLogin ? (
        <div className="space-y-4">
          <div className="flex flex-wrap gap-2">
            <a
              href={`https://www.twitch.tv/${encodeURIComponent(twitchFeedLogin)}`}
              target="_blank"
              rel="noopener noreferrer"
              className="inline-flex items-center gap-2 text-xs font-semibold px-3 py-2 rounded-xl bg-[#6441a4] text-white hover:bg-[#7552b3] transition-colors"
            >
              <ExternalLink className="w-3.5 h-3.5" />
              Открыть на Twitch
            </a>
          </div>
          <TwitchChannelPanel channelLogin={twitchFeedLogin} variant="vkFeedOnly" />
        </div>
      ) : null}

      {tab === 'clips' && (
        <div>
          <p className="text-sm text-gray-500 mb-4 max-w-[min(100%,40rem)] text-pretty">
            {isSelf
              ? `Ваши клипы на сайте.${data.twitch_login ? ' Полный каталог по Twitch — вкладка «Канал Twitch».' : ''}`
              : `Клипы этого пользователя.${data.twitch_login ? ' Каталог Twitch — вкладка «Канал Twitch».' : ''}`}
          </p>
          {tabLoading ? (
            <div className="flex justify-center py-16 text-gray-500">
              <Loader2 className="w-8 h-8 animate-spin" />
            </div>
          ) : authorClips.length === 0 ? (
            <p className="text-center text-gray-500 text-sm py-12">Пока нет клипов.</p>
          ) : (
            <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 2xl:grid-cols-4 gap-4 lg:gap-5">
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

      {tab === 'friends_feed' && isSelf && (
        <div>
          <p className="text-sm text-gray-400 mb-4">
            Клипы, которые недавно смотрели ваши друзья в ClipStream (по активности на сайте).
          </p>
          {tabLoading ? (
            <div className="flex justify-center py-16">
              <Loader2 className="w-8 h-8 animate-spin text-gray-500" />
            </div>
          ) : friendsClips.length === 0 ? (
            <p className="text-gray-500 text-sm text-center py-12">
              Добавьте друзей и смотрите клипы — здесь появится лента их просмотров.
            </p>
          ) : (
            <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 2xl:grid-cols-4 gap-4 lg:gap-5">
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
    </SocialProfileLayout>
  );
}
