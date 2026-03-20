import { useCallback, useEffect, useMemo, useState } from 'react';
import { Link, useNavigate } from 'react-router-dom';
import {
  Activity,
  BarChart3,
  Code2,
  Copy,
  LayoutDashboard,
  MonitorSmartphone,
  RefreshCw,
  Server,
  Shield,
  Terminal,
  Wrench,
  Crown,
} from 'lucide-react';
import useAuthStore from '../store/useAuthStore';
import { api, admin as adminApi, staff as staffApi, moderation as moderationApi } from '../api/client';

const API_REFERENCE = [
  { method: 'GET', path: '/health', auth: 'нет', desc: 'Liveness' },
  { method: 'GET', path: '/auth/me', auth: 'Bearer', desc: 'Профиль и роль' },
  { method: 'GET', path: '/staff/bootstrap', auth: 'Bearer (staff)', desc: 'Метаданные консоли' },
  { method: 'GET', path: '/staff/diagnostics', auth: 'Bearer (staff)', desc: 'MySQL, Redis, uptime' },
  { method: 'GET', path: '/staff/content-reports', auth: 'Bearer (staff)', desc: 'Открытые жалобы на контент' },
  { method: 'GET', path: '/users/public/:username', auth: 'опционально', desc: 'Публичный профиль + история бейджей' },
  { method: 'POST', path: '/social/follow/:userId', auth: 'Bearer', desc: 'Подписка на пользователя платформы' },
  { method: 'DELETE', path: '/social/follow/:userId', auth: 'Bearer', desc: 'Отписка' },
  { method: 'GET', path: '/social/friends-feed', auth: 'Bearer', desc: 'Лента «друзья смотрят»' },
  { method: 'GET', path: '/user/settings', auth: 'Bearer', desc: 'hide_mature, mute_keywords, clip_languages' },
  { method: 'PUT', path: '/user/settings', auth: 'Bearer', desc: 'Сохранить настройки ленты' },
  { method: 'POST', path: '/clips/:id/report', auth: 'Bearer', desc: 'Жалоба на клип { reason }' },
  { method: 'GET', path: '/social/users/:userId/followers', auth: 'нет', desc: 'Подписчики пользователя' },
  { method: 'GET', path: '/social/users/:userId/following', auth: 'нет', desc: 'Подписки пользователя' },
  { method: 'GET', path: '/social/users/:userId/clips', auth: 'нет', desc: 'Клипы с author_id = user' },
  { method: 'GET', path: '/social/users/:userId/watch-activity', auth: 'Bearer (только свой id)', desc: 'История просмотров' },
  { method: 'GET', path: '/users/search?q=', auth: 'нет', desc: 'Поиск пользователей по нику' },
  { method: 'GET', path: '/admin/analytics/summary', auth: 'Bearer (admin)', desc: 'Сводка событий за 24ч' },
  { method: 'GET', path: '/admin/dashboard', auth: 'Bearer (admin)', desc: 'Сводка платформы + аналитика' },
  { method: 'GET', path: '/admin/users', auth: 'Bearer (admin)', desc: 'Список пользователей (q, limit, offset)' },
  { method: 'POST', path: '/admin/users/:id/role', auth: 'Bearer (admin)', desc: 'Назначить роль { role }' },
  { method: 'POST', path: '/moderation/users/:id/restriction', auth: 'Bearer (mod)', desc: 'Бан чата / аккаунта' },
  { method: 'GET', path: '/clips/home', auth: 'опционально', desc: 'Главная лента (hot/fresh/recommended)' },
  { method: 'GET', path: '/clips', auth: 'опционально', desc: 'Лента с фильтрами' },
  { method: 'GET', path: '/clips/:id', auth: 'нет', desc: 'Карточка клипа' },
  { method: 'POST', path: '/clips/:id/view', auth: 'опционально', desc: 'Засчитать просмотр' },
  { method: 'POST', path: '/clips/:id/engagement', auth: 'Bearer', desc: 'Heartbeat / сессия просмотра' },
  { method: 'POST', path: '/clips/:id/feedback', auth: 'Bearer', desc: 'Скрыть клип/канал/игру' },
  { method: 'GET', path: '/channels/similar', auth: 'Bearer', desc: 'Похожие каналы' },
  { method: 'GET', path: '/channels/profile?login=', auth: 'нет', desc: 'Профиль канала (Twitch + число клипов)' },
  { method: 'GET', path: '/streams/live', auth: 'нет', desc: 'Онлайн-стримы' },
];

function JsonBlock({ title, data, onRefresh, refreshing }) {
  const text = data == null ? '' : JSON.stringify(data, null, 2);
  const copy = () => {
    if (!text) return;
    navigator.clipboard.writeText(text).catch(() => {});
  };
  return (
    <div className="rounded-xl border border-brand-700/50 bg-brand-900/50 overflow-hidden">
      <div className="flex items-center justify-between gap-2 px-4 py-2.5 border-b border-brand-700/40 bg-brand-800/40">
        <span className="text-xs font-semibold text-gray-300 uppercase tracking-wide">{title}</span>
        <div className="flex items-center gap-1">
          {onRefresh && (
            <button
              type="button"
              onClick={onRefresh}
              disabled={refreshing}
              className="p-1.5 rounded-lg text-gray-400 hover:text-white hover:bg-brand-700/60 disabled:opacity-50"
              title="Обновить"
            >
              <RefreshCw className={`w-3.5 h-3.5 ${refreshing ? 'animate-spin' : ''}`} />
            </button>
          )}
          <button
            type="button"
            onClick={copy}
            className="p-1.5 rounded-lg text-gray-400 hover:text-white hover:bg-brand-700/60"
            title="Копировать JSON"
          >
            <Copy className="w-3.5 h-3.5" />
          </button>
        </div>
      </div>
      <pre className="p-4 text-[11px] leading-relaxed text-emerald-200/90 overflow-x-auto max-h-[min(420px,50vh)]">
        {text || '—'}
      </pre>
    </div>
  );
}

export default function StaffConsole() {
  const navigate = useNavigate();
  const { isAuth, user, profile, fetchMe } = useAuthStore();
  const [active, setActive] = useState('overview');
  const [bootstrap, setBootstrap] = useState(null);
  const [diagnostics, setDiagnostics] = useState(null);
  const [analytics, setAnalytics] = useState(null);
  const [publicHealth, setPublicHealth] = useState(null);
  const [err, setErr] = useState('');
  const [loading, setLoading] = useState(true);
  const [refreshing, setRefreshing] = useState(false);
  const [modUserId, setModUserId] = useState('');
  const [modAction, setModAction] = useState('chat_ban');
  const [modDays, setModDays] = useState(7);
  const [modPermanent, setModPermanent] = useState(false);
  const [modReason, setModReason] = useState('');
  const [modMsg, setModMsg] = useState('');
  const [modSending, setModSending] = useState(false);

  const role = user?.role ?? 0;
  const isStaff = role === 1 || role === 2;
  const isAdmin = role === 2;
  const canModerate = role === 1 || role === 2;

  useEffect(() => {
    if (isAuth && !user) fetchMe();
  }, [isAuth, user, fetchMe]);

  const loadCore = useCallback(async () => {
    setErr('');
    setRefreshing(true);
    try {
      const [b, d, h] = await Promise.all([
        staffApi.bootstrap(),
        staffApi.diagnostics(),
        api.get('/health'),
      ]);
      setBootstrap(b);
      setDiagnostics(d);
      setPublicHealth(h);
      if (isAdmin) {
        const a = await adminApi.analyticsSummary();
        setAnalytics(a);
      } else setAnalytics(null);
    } catch (e) {
      setErr(e.message || 'Ошибка загрузки');
    } finally {
      setRefreshing(false);
      setLoading(false);
    }
  }, [isAdmin]);

  useEffect(() => {
    if (!isAuth) {
      setLoading(false);
      return;
    }
    if (user == null) return;
    if (!isStaff) {
      setLoading(false);
      return;
    }
    loadCore();
  }, [isAuth, user, isStaff, loadCore]);

  const clientInfo = useMemo(
    () => ({
      href: typeof window !== 'undefined' ? window.location.href : '',
      userAgent: typeof navigator !== 'undefined' ? navigator.userAgent : '',
      language: typeof navigator !== 'undefined' ? navigator.language : '',
      viewport:
        typeof window !== 'undefined'
          ? { w: window.innerWidth, h: window.innerHeight }
          : null,
      tokenPresent: typeof localStorage !== 'undefined' && !!localStorage.getItem('token'),
      userRole: role,
      profileKeys: profile && typeof profile === 'object' ? Object.keys(profile) : [],
    }),
    [role, profile]
  );

  if (!isAuth) {
    return (
      <div className="p-8 max-w-lg mx-auto text-center">
        <Shield className="w-14 h-14 text-gray-600 mx-auto mb-4" />
        <h1 className="text-xl font-bold text-white mb-2">Нужна авторизация</h1>
        <p className="text-gray-400 text-sm mb-6">Войдите через Twitch, чтобы открыть консоль персонала.</p>
        <Link to="/" className="text-brand-400 hover:underline text-sm">
          На главную
        </Link>
      </div>
    );
  }

  if (isAuth && user == null) {
    return (
      <div className="p-8 max-w-lg mx-auto text-center text-gray-400 text-sm">
        <RefreshCw className="w-8 h-8 mx-auto mb-3 animate-spin text-brand-500" />
        Загрузка профиля…
      </div>
    );
  }

  if (!loading && user != null && !isStaff) {
    return (
      <div className="p-8 max-w-lg mx-auto text-center">
        <Shield className="w-14 h-14 text-amber-600/80 mx-auto mb-4" />
        <h1 className="text-xl font-bold text-white mb-2">Доступ запрещён</h1>
        <p className="text-gray-400 text-sm mb-6">
          Консоль доступна модераторам и администраторам. Ваша текущая роль: пользователь.
        </p>
        <button
          type="button"
          onClick={() => navigate(-1)}
          className="text-brand-400 hover:underline text-sm"
        >
          Назад
        </button>
      </div>
    );
  }

  const tabs = [
    { id: 'overview', label: 'Обзор', icon: LayoutDashboard },
    { id: 'system', label: 'Система', icon: Server },
    ...(isAdmin ? [{ id: 'analytics', label: 'Аналитика', icon: BarChart3 }] : []),
    ...(canModerate ? [{ id: 'moderation', label: 'Модерация', icon: Shield }] : []),
    { id: 'api', label: 'Справочник API', icon: Code2 },
    { id: 'client', label: 'Клиент', icon: MonitorSmartphone },
  ];

  const submitModeration = async (e) => {
    e.preventDefault();
    setModMsg('');
    const id = parseInt(modUserId, 10);
    if (!id) {
      setModMsg('Укажите числовой ID пользователя');
      return;
    }
    setModSending(true);
    try {
      const body = { action: modAction, reason: modReason };
      if (modAction === 'chat_ban') {
        body.days = modDays;
        body.permanent = modPermanent;
      }
      await moderationApi.restriction(id, body);
      setModMsg('Готово');
    } catch (err) {
      setModMsg(err.message || 'Ошибка');
    } finally {
      setModSending(false);
    }
  };

  return (
    <div className="p-4 lg:p-8 max-w-6xl mx-auto pb-16">
      <div className="flex flex-col sm:flex-row sm:items-end sm:justify-between gap-4 mb-8">
        <div>
          <div className="flex items-center gap-2 text-brand-400 text-xs font-semibold uppercase tracking-wider mb-1">
            <Wrench className="w-3.5 h-3.5" />
            Staff &amp; Dev
          </div>
          <h1 className="text-2xl lg:text-3xl font-black text-white tracking-tight">Консоль разработчика</h1>
          <p className="text-gray-400 text-sm mt-1 max-w-xl">
            Диагностика, аналитика и справочник эндпоинтов для модераторов и администраторов.
          </p>
        </div>
        <button
          type="button"
          onClick={loadCore}
          disabled={refreshing || !isStaff}
          className="inline-flex items-center justify-center gap-2 px-4 py-2 rounded-xl bg-brand-600 hover:bg-brand-500 text-white text-sm font-semibold disabled:opacity-50"
        >
          <RefreshCw className={`w-4 h-4 ${refreshing ? 'animate-spin' : ''}`} />
          Обновить данные
        </button>
      </div>

      {err && (
        <div className="mb-6 rounded-xl border border-red-500/40 bg-red-950/40 px-4 py-3 text-sm text-red-200">
          {err}
        </div>
      )}

      <div className="flex flex-col lg:flex-row gap-6">
        <nav className="lg:w-52 shrink-0 flex lg:flex-col gap-1 overflow-x-auto pb-1 lg:pb-0">
          {tabs.map((t) => {
            const Icon = t.icon;
            const on = active === t.id;
            return (
              <button
                key={t.id}
                type="button"
                onClick={() => setActive(t.id)}
                className={`flex items-center gap-2 px-3 py-2.5 rounded-xl text-sm font-medium whitespace-nowrap transition-colors ${
                  on
                    ? 'bg-brand-600 text-white shadow-lg shadow-brand-600/20'
                    : 'text-gray-400 hover:text-white hover:bg-brand-800/70'
                }`}
              >
                <Icon className="w-4 h-4 shrink-0" />
                {t.label}
              </button>
            );
          })}
        </nav>

        <div className="flex-1 min-w-0 space-y-6">
          {loading && isStaff && (
            <div className="rounded-xl border border-brand-700/50 bg-brand-900/40 p-8 text-center text-gray-400 text-sm">
              Загрузка консоли…
            </div>
          )}

          {!loading && active === 'overview' && (
            <div className="space-y-4 animate-fade-in">
              <div className="grid grid-cols-1 sm:grid-cols-2 gap-4">
                <div className="rounded-xl border border-brand-700/50 bg-brand-800/50 p-5">
                  <div className="flex items-center gap-2 text-gray-400 text-xs uppercase font-semibold mb-2">
                    <Shield className="w-4 h-4 text-violet-400" />
                    Роль
                  </div>
                  <p className="text-2xl font-bold text-white capitalize">{bootstrap?.role ?? '—'}</p>
                  <p className="text-xs text-gray-500 mt-1">user_id: {bootstrap?.user_id ?? user?.id}</p>
                </div>
                <div className="rounded-xl border border-brand-700/50 bg-brand-800/50 p-5">
                  <div className="flex items-center gap-2 text-gray-400 text-xs uppercase font-semibold mb-2">
                    <Terminal className="w-4 h-4 text-emerald-400" />
                    Сборка
                  </div>
                  <p className="text-2xl font-bold text-white">{bootstrap?.version ?? '—'}</p>
                  <p className="text-xs text-gray-500 mt-1">Сервер (UTC): {bootstrap?.server_time_utc ?? '—'}</p>
                </div>
              </div>
              <div className="rounded-xl border border-brand-700/50 bg-brand-800/40 p-5">
                <h3 className="text-white font-semibold mb-3 flex items-center gap-2">
                  <Activity className="w-4 h-4 text-sky-400" />
                  Возможности
                </h3>
                <ul className="grid sm:grid-cols-2 gap-2 text-sm">
                  {Object.entries(bootstrap?.capabilities || {}).map(([k, v]) => (
                    <li
                      key={k}
                      className="flex items-center justify-between gap-2 rounded-lg bg-brand-900/50 px-3 py-2 border border-brand-700/30"
                    >
                      <span className="text-gray-400">{k}</span>
                      <span className={v ? 'text-emerald-400' : 'text-gray-600'}>{v ? 'да' : 'нет'}</span>
                    </li>
                  ))}
                </ul>
              </div>
              {isAdmin && (
                <Link
                  to="/admin"
                  className="flex items-center gap-3 rounded-xl border border-amber-500/35 bg-amber-500/10 px-5 py-4 text-amber-100 hover:bg-amber-500/15 transition-colors"
                >
                  <Crown className="w-6 h-6 text-amber-400 shrink-0" />
                  <div>
                    <p className="font-bold text-white">Панель администратора</p>
                    <p className="text-sm text-amber-200/80">Статистика платформы, роли пользователей, аналитика</p>
                  </div>
                </Link>
              )}
              <JsonBlock title="bootstrap (raw)" data={bootstrap} onRefresh={loadCore} refreshing={refreshing} />
            </div>
          )}

          {!loading && active === 'system' && (
            <div className="space-y-4">
              <div className="grid grid-cols-1 sm:grid-cols-3 gap-3">
                <div className="rounded-xl border border-brand-700/50 bg-brand-800/50 p-4 text-center">
                  <p className="text-xs text-gray-500 mb-1">MySQL</p>
                  <p className={`text-lg font-bold ${diagnostics?.mysql_ok ? 'text-emerald-400' : 'text-red-400'}`}>
                    {diagnostics?.mysql_ok ? 'OK' : 'FAIL'}
                  </p>
                </div>
                <div className="rounded-xl border border-brand-700/50 bg-brand-800/50 p-4 text-center">
                  <p className="text-xs text-gray-500 mb-1">Redis</p>
                  <p className={`text-lg font-bold ${diagnostics?.redis_ok ? 'text-emerald-400' : 'text-red-400'}`}>
                    {diagnostics?.redis_ok ? 'OK' : 'FAIL'}
                  </p>
                </div>
                <div className="rounded-xl border border-brand-700/50 bg-brand-800/50 p-4 text-center">
                  <p className="text-gray-500 mb-1 text-xs">Uptime</p>
                  <p className="text-lg font-bold text-white">
                    {diagnostics?.uptime_seconds != null ? `${diagnostics.uptime_seconds}s` : '—'}
                  </p>
                </div>
              </div>
              <JsonBlock title="Публичный /health" data={publicHealth} />
              <JsonBlock title="staff/diagnostics" data={diagnostics} onRefresh={loadCore} refreshing={refreshing} />
            </div>
          )}

          {!loading && active === 'analytics' && isAdmin && (
            <div className="space-y-4">
              <p className="text-sm text-gray-400">
                Агрегаты из <code className="text-brand-300 bg-brand-900/80 px-1 rounded">analytics_*</code> за последние
                сутки / 7 дней.
              </p>
              <JsonBlock title="admin/analytics/summary" data={analytics} onRefresh={loadCore} refreshing={refreshing} />
            </div>
          )}

          {!loading && active === 'moderation' && canModerate && (
            <div className="space-y-4 max-w-lg">
              <p className="text-sm text-gray-400">
                Ограничения чата, блокировка аккаунта. Действия: <code className="text-brand-300">chat_ban</code>,{' '}
                <code className="text-brand-300">lift_chat</code>, <code className="text-brand-300">block_account</code>,{' '}
                <code className="text-brand-300">unblock_account</code>.
              </p>
              <form onSubmit={submitModeration} className="rounded-xl border border-brand-700/50 bg-brand-800/40 p-5 space-y-3">
                <div>
                  <label className="text-xs text-gray-500 block mb-1">ID пользователя</label>
                  <input
                    value={modUserId}
                    onChange={(e) => setModUserId(e.target.value)}
                    className="w-full bg-brand-900 border border-brand-600 rounded-lg px-3 py-2 text-sm text-white"
                    placeholder="например 42"
                  />
                </div>
                <div>
                  <label className="text-xs text-gray-500 block mb-1">Действие</label>
                  <select
                    value={modAction}
                    onChange={(e) => setModAction(e.target.value)}
                    className="w-full bg-brand-900 border border-brand-600 rounded-lg px-3 py-2 text-sm text-white"
                  >
                    <option value="chat_ban">Мут чата (временный / навсегда)</option>
                    <option value="lift_chat">Снять ограничение чата</option>
                    <option value="block_account">Заблокировать аккаунт</option>
                    <option value="unblock_account">Разблокировать аккаунт</option>
                  </select>
                </div>
                {modAction === 'chat_ban' && (
                  <>
                    <label className="flex items-center gap-2 text-sm text-gray-300">
                      <input
                        type="checkbox"
                        checked={modPermanent}
                        onChange={(e) => setModPermanent(e.target.checked)}
                      />
                      Навсегда
                    </label>
                    {!modPermanent && (
                      <div>
                        <label className="text-xs text-gray-500 block mb-1">Дней</label>
                        <input
                          type="number"
                          min={1}
                          max={365}
                          value={modDays}
                          onChange={(e) => setModDays(parseInt(e.target.value, 10) || 7)}
                          className="w-full bg-brand-900 border border-brand-600 rounded-lg px-3 py-2 text-sm text-white"
                        />
                      </div>
                    )}
                  </>
                )}
                <div>
                  <label className="text-xs text-gray-500 block mb-1">Причина (опционально)</label>
                  <input
                    value={modReason}
                    onChange={(e) => setModReason(e.target.value)}
                    className="w-full bg-brand-900 border border-brand-600 rounded-lg px-3 py-2 text-sm text-white"
                  />
                </div>
                {modMsg && (
                  <p className={`text-sm ${modMsg === 'Готово' ? 'text-emerald-400' : 'text-red-400'}`}>{modMsg}</p>
                )}
                <button
                  type="submit"
                  disabled={modSending}
                  className="w-full py-2.5 rounded-lg bg-brand-600 hover:bg-brand-500 text-white text-sm font-semibold disabled:opacity-50"
                >
                  {modSending ? 'Отправка…' : 'Применить'}
                </button>
              </form>
            </div>
          )}

          {!loading && active === 'api' && (
            <div className="rounded-xl border border-brand-700/50 overflow-hidden">
              <table className="w-full text-sm">
                <thead>
                  <tr className="bg-brand-800/80 text-left text-xs uppercase text-gray-500">
                    <th className="px-4 py-3 font-semibold">Метод</th>
                    <th className="px-4 py-3 font-semibold">Путь</th>
                    <th className="px-4 py-3 font-semibold">Auth</th>
                    <th className="px-4 py-3 font-semibold hidden md:table-cell">Описание</th>
                  </tr>
                </thead>
                <tbody>
                  {API_REFERENCE.map((row) => (
                    <tr key={`${row.method}-${row.path}`} className="border-t border-brand-700/40 hover:bg-brand-800/30">
                      <td className="px-4 py-2.5 font-mono text-amber-300/90">{row.method}</td>
                      <td className="px-4 py-2.5 font-mono text-cyan-200/90 break-all">{row.path}</td>
                      <td className="px-4 py-2.5 text-gray-400">{row.auth}</td>
                      <td className="px-4 py-2.5 text-gray-400 hidden md:table-cell">{row.desc}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          )}

          {!loading && active === 'client' && (
            <JsonBlock title="Окружение браузера (без секретов)" data={clientInfo} />
          )}
        </div>
      </div>
    </div>
  );
}
