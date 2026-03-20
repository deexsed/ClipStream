import { useCallback, useEffect, useState } from 'react';
import { Link, useNavigate } from 'react-router-dom';
import {
  ArrowLeft,
  BarChart3,
  ChevronLeft,
  ChevronRight,
  Crown,
  LayoutDashboard,
  Megaphone,
  RefreshCw,
  Search,
  Shield,
  Users,
} from 'lucide-react';
import useAuthStore from '../store/useAuthStore';
import { admin as adminApi } from '../api/client';

const PAGE = 30;

function StatCard({ icon: Icon, label, value, sub }) {
  return (
    <div className="rounded-xl border border-brand-700/50 bg-brand-800/50 p-4">
      <div className="flex items-center gap-2 text-gray-500 text-xs uppercase font-semibold mb-2">
        <Icon className="w-4 h-4 text-brand-400" />
        {label}
      </div>
      <p className="text-2xl font-bold text-white tabular-nums">{value ?? '—'}</p>
      {sub && <p className="text-xs text-gray-500 mt-1">{sub}</p>}
    </div>
  );
}

export default function AdminDashboard() {
  const navigate = useNavigate();
  const { isAuth, user, fetchMe } = useAuthStore();
  const [tab, setTab] = useState('overview');
  const [dash, setDash] = useState(null);
  const [userRows, setUserRows] = useState([]);
  const [totalUsers, setTotalUsers] = useState(0);
  const [offset, setOffset] = useState(0);
  const [search, setSearch] = useState('');
  const [searchInput, setSearchInput] = useState('');
  const [loading, setLoading] = useState(true);
  const [savingId, setSavingId] = useState(null);
  const [pendingRoles, setPendingRoles] = useState({});
  const [msg, setMsg] = useState('');
  const [promotions, setPromotions] = useState([]);
  const [promoForm, setPromoForm] = useState({
    clip_id: '',
    boost_score: '45',
    placement: 'recommended',
    label: '',
    starts_at: '',
    ends_at: '',
  });
  const [promoSaving, setPromoSaving] = useState(false);

  const isAdmin = (user?.role ?? 0) === 2;

  useEffect(() => {
    if (isAuth && !user) fetchMe();
  }, [isAuth, user, fetchMe]);

  useEffect(() => {
    if (!isAuth || user == null) return;
    if (!isAdmin) navigate('/', { replace: true });
  }, [isAuth, user, isAdmin, navigate]);

  const loadDashboard = useCallback(async () => {
    const d = await adminApi.dashboard();
    setDash(d);
  }, []);

  const loadUsers = useCallback(async () => {
    const params = new URLSearchParams({
      limit: String(PAGE),
      offset: String(offset),
    });
    if (search.trim()) params.set('q', search.trim());
    const data = await adminApi.users(params.toString());
    setUserRows(data.users || []);
    setTotalUsers(data.total ?? 0);
    setPendingRoles({});
  }, [offset, search]);

  const loadPromotions = useCallback(async () => {
    const data = await adminApi.promotions('limit=100');
    setPromotions(data.promotions || []);
  }, []);

  const refreshAll = useCallback(async () => {
    setLoading(true);
    setMsg('');
    try {
      await Promise.all([loadDashboard(), loadUsers(), loadPromotions()]);
    } catch (e) {
      setMsg(e.message || 'Ошибка загрузки');
    } finally {
      setLoading(false);
    }
  }, [loadDashboard, loadUsers, loadPromotions]);

  useEffect(() => {
    if (!isAuth || user == null || !isAdmin) return;
    setLoading(true);
    setMsg('');
    Promise.all([loadDashboard(), loadPromotions()])
      .catch((e) => setMsg(e.message || 'Ошибка'))
      .finally(() => setLoading(false));
  }, [isAuth, user, isAdmin, loadDashboard, loadPromotions]);

  useEffect(() => {
    if (tab !== 'promotions' || !isAuth || !isAdmin) return;
    loadPromotions().catch((e) => setMsg(e.message || 'Ошибка'));
  }, [tab, isAuth, isAdmin, loadPromotions]);

  useEffect(() => {
    if (!isAuth || user == null || !isAdmin) return;
    loadUsers().catch((e) => setMsg(e.message || 'Ошибка'));
  }, [offset, search, isAuth, user, isAdmin, loadUsers]);

  const applyRole = async (userId) => {
    const row = userRows.find((x) => x.id === userId);
    if (!row) return;
    const role = pendingRoles[userId] ?? row.role;
    if (role === row.role) return;
    setSavingId(userId);
    setMsg('');
    try {
      await adminApi.setUserRole(userId, role);
      await loadDashboard();
      await loadUsers();
    } catch (e) {
      setMsg(e.message || 'Не удалось сменить роль');
    } finally {
      setSavingId(null);
    }
  };

  const ov = dash?.overview || {};
  const an = dash?.analytics_24h || {};

  if (!isAuth) {
    return (
      <div className="p-8 text-center text-gray-400">
        <Shield className="w-12 h-12 mx-auto mb-3 text-gray-600" />
        Войдите в аккаунт.
      </div>
    );
  }

  if (user == null) {
    return (
      <div className="p-8 text-center text-gray-400">
        <RefreshCw className="w-8 h-8 mx-auto animate-spin text-brand-500" />
      </div>
    );
  }

  if (!isAdmin) {
    return null;
  }

  return (
    <div className="p-4 lg:p-8 max-w-6xl mx-auto pb-20">
      <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-4 mb-8">
        <div>
          <Link
            to="/staff"
            className="inline-flex items-center gap-1.5 text-xs text-gray-500 hover:text-brand-400 mb-2"
          >
            <ArrowLeft className="w-3.5 h-3.5" />
            Назад в консоль
          </Link>
          <div className="flex items-center gap-2 text-amber-400/90 text-xs font-bold uppercase tracking-wider">
            <Crown className="w-4 h-4" />
            Администратор
          </div>
          <h1 className="text-2xl lg:text-3xl font-black text-white tracking-tight">Панель администратора</h1>
          <p className="text-gray-400 text-sm mt-1">Статистика платформы и управление ролями пользователей.</p>
        </div>
        <button
          type="button"
          onClick={refreshAll}
          disabled={loading}
          className="inline-flex items-center gap-2 px-4 py-2 rounded-xl bg-brand-600 hover:bg-brand-500 text-white text-sm font-semibold disabled:opacity-50"
        >
          <RefreshCw className={`w-4 h-4 ${loading ? 'animate-spin' : ''}`} />
          Обновить
        </button>
      </div>

      {msg && (
        <div className="mb-4 rounded-xl border border-red-500/40 bg-red-950/35 px-4 py-3 text-sm text-red-200">
          {msg}
        </div>
      )}

      <div className="flex gap-1 mb-6 p-1 rounded-xl bg-brand-900/60 border border-brand-700/50 w-fit">
        <button
          type="button"
          onClick={() => setTab('overview')}
          className={`flex items-center gap-2 px-4 py-2 rounded-lg text-sm font-medium ${
            tab === 'overview' ? 'bg-brand-600 text-white' : 'text-gray-400 hover:text-white'
          }`}
        >
          <LayoutDashboard className="w-4 h-4" />
          Статистика
        </button>
        <button
          type="button"
          onClick={() => setTab('users')}
          className={`flex items-center gap-2 px-4 py-2 rounded-lg text-sm font-medium ${
            tab === 'users' ? 'bg-brand-600 text-white' : 'text-gray-400 hover:text-white'
          }`}
        >
          <Users className="w-4 h-4" />
          Пользователи
        </button>
        <button
          type="button"
          onClick={() => setTab('promotions')}
          className={`flex items-center gap-2 px-4 py-2 rounded-lg text-sm font-medium ${
            tab === 'promotions' ? 'bg-brand-600 text-white' : 'text-gray-400 hover:text-white'
          }`}
        >
          <Megaphone className="w-4 h-4" />
          Продвижение
        </button>
      </div>

      {tab === 'overview' && (
        <div className="space-y-6 animate-fade-in">
          <div className="grid grid-cols-2 lg:grid-cols-5 gap-3">
            <StatCard icon={Users} label="Пользователей" value={ov.users_total} />
            <StatCard icon={Shield} label="Админов" value={ov.users_admin} />
            <StatCard icon={Shield} label="Модераторов" value={ov.users_moderator} />
            <StatCard icon={Users} label="VIP" value={ov.users_vip} />
            <StatCard icon={BarChart3} label="Просмотров 24ч" value={ov.views_24h} />
          </div>
          <div className="grid grid-cols-2 gap-3">
            <StatCard icon={LayoutDashboard} label="Клипов" value={ov.clips_total} />
            <StatCard icon={Users} label="Комментариев" value={ov.comments_total} />
          </div>

          <div className="rounded-xl border border-brand-700/50 bg-brand-900/40 p-4">
            <h3 className="text-white font-bold flex items-center gap-2 mb-3">
              <BarChart3 className="w-4 h-4 text-sky-400" />
              События аналитики (24ч)
            </h3>
            <pre className="text-xs text-emerald-200/90 overflow-x-auto">
              {JSON.stringify(an.feed_events_24h || {}, null, 2)}
            </pre>
            <div className="mt-4 grid sm:grid-cols-2 gap-3 text-sm text-gray-400">
              <p>
                Сессий (last_seen 24ч):{' '}
                <span className="text-white font-mono">{an.sessions_touched_24h ?? '—'}</span>
              </p>
              <p>
                User daily (7д):{' '}
                <span className="text-white font-mono">
                  {an.user_daily_7d ? JSON.stringify(an.user_daily_7d) : '—'}
                </span>
              </p>
            </div>
          </div>
        </div>
      )}

      {tab === 'users' && (
        <div className="space-y-4 animate-fade-in">
          <form
            className="flex flex-col sm:flex-row gap-2"
            onSubmit={(e) => {
              e.preventDefault();
              setOffset(0);
              setSearch(searchInput);
            }}
          >
            <div className="relative flex-1">
              <Search className="absolute left-3 top-2.5 w-4 h-4 text-gray-500" />
              <input
                value={searchInput}
                onChange={(e) => setSearchInput(e.target.value)}
                placeholder="Поиск по имени или email…"
                className="w-full bg-brand-800/80 border border-brand-700 rounded-xl py-2 pl-10 pr-4 text-sm text-white placeholder-gray-500"
              />
            </div>
            <button
              type="submit"
              className="px-4 py-2 rounded-xl bg-brand-600 hover:bg-brand-500 text-white text-sm font-semibold"
            >
              Найти
            </button>
          </form>

          <div className="rounded-xl border border-brand-700/50 overflow-hidden">
            <div className="overflow-x-auto">
              <table className="w-full text-sm">
                <thead>
                  <tr className="bg-brand-800/90 text-left text-xs uppercase text-gray-500">
                    <th className="px-3 py-2 font-semibold">ID</th>
                    <th className="px-3 py-2 font-semibold">Пользователь</th>
                    <th className="px-3 py-2 font-semibold hidden md:table-cell">Email</th>
                    <th className="px-3 py-2 font-semibold">Роль</th>
                    <th className="px-3 py-2 font-semibold" />
                  </tr>
                </thead>
                <tbody>
                  {userRows.map((u) => {
                    const current = pendingRoles[u.id] ?? u.role;
                    return (
                      <tr key={u.id} className="border-t border-brand-700/40 hover:bg-brand-800/25">
                        <td className="px-3 py-2 font-mono text-gray-500">{u.id}</td>
                        <td className="px-3 py-2">
                          <div className="flex items-center gap-2">
                            <img
                              src={u.avatar_url || `https://api.dicebear.com/7.x/avataaars/svg?seed=${u.username}`}
                              alt=""
                              className="w-8 h-8 rounded-full bg-brand-900"
                            />
                            <span className="text-white font-medium">{u.username}</span>
                          </div>
                        </td>
                        <td className="px-3 py-2 text-gray-400 hidden md:table-cell">{u.email}</td>
                        <td className="px-3 py-2">
                          <select
                            value={current}
                            onChange={(e) =>
                              setPendingRoles((prev) => ({ ...prev, [u.id]: e.target.value }))
                            }
                            className="bg-brand-900 border border-brand-600 rounded-lg px-2 py-1 text-xs text-white"
                          >
                            <option value="user">user</option>
                            <option value="moderator">moderator</option>
                            <option value="admin">admin</option>
                            <option value="vip">vip</option>
                          </select>
                        </td>
                        <td className="px-3 py-2 text-right">
                          <button
                            type="button"
                            disabled={savingId === u.id || current === u.role}
                            onClick={() => applyRole(u.id)}
                            className="text-xs font-bold px-3 py-1.5 rounded-lg bg-brand-600 hover:bg-brand-500 text-white disabled:opacity-40 disabled:cursor-not-allowed"
                          >
                            {savingId === u.id ? '…' : 'Сохранить'}
                          </button>
                        </td>
                      </tr>
                    );
                  })}
                </tbody>
              </table>
            </div>
            <div className="flex items-center justify-between px-4 py-3 border-t border-brand-700/40 bg-brand-900/40">
              <span className="text-xs text-gray-500">
                Всего: {totalUsers} · страница {Math.floor(offset / PAGE) + 1}
              </span>
              <div className="flex gap-2">
                <button
                  type="button"
                  disabled={offset === 0}
                  onClick={() => setOffset((o) => Math.max(0, o - PAGE))}
                  className="p-2 rounded-lg border border-brand-700 text-gray-400 hover:text-white disabled:opacity-30"
                >
                  <ChevronLeft className="w-4 h-4" />
                </button>
                <button
                  type="button"
                  disabled={offset + PAGE >= totalUsers}
                  onClick={() => setOffset((o) => o + PAGE)}
                  className="p-2 rounded-lg border border-brand-700 text-gray-400 hover:text-white disabled:opacity-30"
                >
                  <ChevronRight className="w-4 h-4" />
                </button>
              </div>
            </div>
          </div>
        </div>
      )}

      {tab === 'promotions' && (
        <div className="space-y-6 animate-fade-in">
          <p className="text-sm text-gray-400">
            <code className="text-gray-300">all</code> — все слоты; <code className="text-gray-300">hot</code>,{' '}
            <code className="text-gray-300">fresh</code>, <code className="text-gray-300">recommended</code> — блоки
            смешанной главной; <code className="text-gray-300">feed</code> — буст и в горячем, и в новинках, и в
            «для вас»; <code className="text-gray-300">explore</code> — слот «Открытие» (случайные карточки);{' '}
            <code className="text-gray-300">related</code> — колонка на странице клипа. Даты UTC{' '}
            <span className="text-gray-300">YYYY-MM-DD HH:MM:SS</span> или пусто = без ограничения.
          </p>

          <form
            className="rounded-xl border border-brand-700/50 bg-brand-900/40 p-4 grid sm:grid-cols-2 lg:grid-cols-3 gap-3"
            onSubmit={async (e) => {
              e.preventDefault();
              setPromoSaving(true);
              setMsg('');
              try {
                const clipId = Number(promoForm.clip_id);
                if (!clipId) throw new Error('Укажите clip_id');
                const body = {
                  clip_id: clipId,
                  boost_score: Number(promoForm.boost_score) || 45,
                  placement: promoForm.placement,
                  label: promoForm.label.trim(),
                };
                if (promoForm.starts_at.trim()) body.starts_at = promoForm.starts_at.trim();
                if (promoForm.ends_at.trim()) body.ends_at = promoForm.ends_at.trim();
                await adminApi.createPromotion(body);
                setPromoForm((f) => ({ ...f, clip_id: '', label: '' }));
                await loadPromotions();
              } catch (err) {
                setMsg(err.message || 'Ошибка');
              } finally {
                setPromoSaving(false);
              }
            }}
          >
            <div>
              <label className="text-xs text-gray-500 block mb-1">clip_id</label>
              <input
                value={promoForm.clip_id}
                onChange={(e) => setPromoForm((f) => ({ ...f, clip_id: e.target.value }))}
                className="w-full bg-brand-800 border border-brand-600 rounded-lg px-3 py-2 text-sm text-white"
                placeholder="ID клипа в БД"
              />
            </div>
            <div>
              <label className="text-xs text-gray-500 block mb-1">boost_score</label>
              <input
                value={promoForm.boost_score}
                onChange={(e) => setPromoForm((f) => ({ ...f, boost_score: e.target.value }))}
                className="w-full bg-brand-800 border border-brand-600 rounded-lg px-3 py-2 text-sm text-white"
              />
            </div>
            <div>
              <label className="text-xs text-gray-500 block mb-1">placement</label>
              <select
                value={promoForm.placement}
                onChange={(e) => setPromoForm((f) => ({ ...f, placement: e.target.value }))}
                className="w-full bg-brand-800 border border-brand-600 rounded-lg px-3 py-2 text-sm text-white"
              >
                <option value="all">all</option>
                <option value="hot">hot</option>
                <option value="fresh">fresh</option>
                <option value="recommended">recommended</option>
                <option value="feed">feed</option>
                <option value="explore">explore</option>
                <option value="related">related</option>
              </select>
            </div>
            <div className="sm:col-span-2">
              <label className="text-xs text-gray-500 block mb-1">Подпись на карточке (опционально)</label>
              <input
                value={promoForm.label}
                onChange={(e) => setPromoForm((f) => ({ ...f, label: e.target.value }))}
                className="w-full bg-brand-800 border border-brand-600 rounded-lg px-3 py-2 text-sm text-white"
                placeholder="Например: Выбор редакции"
                maxLength={64}
              />
            </div>
            <div>
              <label className="text-xs text-gray-500 block mb-1">starts_at</label>
              <input
                value={promoForm.starts_at}
                onChange={(e) => setPromoForm((f) => ({ ...f, starts_at: e.target.value }))}
                className="w-full bg-brand-800 border border-brand-600 rounded-lg px-3 py-2 text-sm text-white"
                placeholder="опционально"
              />
            </div>
            <div>
              <label className="text-xs text-gray-500 block mb-1">ends_at</label>
              <input
                value={promoForm.ends_at}
                onChange={(e) => setPromoForm((f) => ({ ...f, ends_at: e.target.value }))}
                className="w-full bg-brand-800 border border-brand-600 rounded-lg px-3 py-2 text-sm text-white"
                placeholder="опционально"
              />
            </div>
            <div className="flex items-end">
              <button
                type="submit"
                disabled={promoSaving}
                className="w-full py-2 rounded-lg bg-amber-600 hover:bg-amber-500 text-white text-sm font-bold disabled:opacity-50"
              >
                {promoSaving ? '…' : 'Добавить продвижение'}
              </button>
            </div>
          </form>

          <div className="rounded-xl border border-brand-700/50 overflow-hidden">
            <div className="overflow-x-auto">
              <table className="w-full text-sm">
                <thead>
                  <tr className="bg-brand-800/90 text-left text-xs uppercase text-gray-500">
                    <th className="px-3 py-2 font-semibold">ID</th>
                    <th className="px-3 py-2 font-semibold">клип</th>
                    <th className="px-3 py-2 font-semibold">boost</th>
                    <th className="px-3 py-2 font-semibold">slot</th>
                    <th className="px-3 py-2 font-semibold hidden md:table-cell">label</th>
                    <th className="px-3 py-2 font-semibold" />
                  </tr>
                </thead>
                <tbody>
                  {promotions.map((p) => (
                    <tr key={p.id} className="border-t border-brand-700/40">
                      <td className="px-3 py-2 font-mono text-gray-500">{p.id}</td>
                      <td className="px-3 py-2 text-white">
                        <span className="font-mono text-gray-400">{p.clip_id}</span>
                        {p.clip_title ? (
                          <div className="text-xs text-gray-300 font-normal mt-0.5 line-clamp-2" title={p.clip_title}>
                            {p.clip_title}
                          </div>
                        ) : null}
                        {p.broadcaster_name ? (
                          <div className="text-[11px] text-gray-500 truncate">{p.broadcaster_name}</div>
                        ) : null}
                      </td>
                      <td className="px-3 py-2 text-gray-300">{p.boost_score}</td>
                      <td className="px-3 py-2 text-brand-300">{p.placement}</td>
                      <td className="px-3 py-2 text-gray-400 hidden md:table-cell">{p.label || '—'}</td>
                      <td className="px-3 py-2 text-right">
                        <button
                          type="button"
                          className="text-xs text-red-400 hover:text-red-300 font-bold"
                          onClick={async () => {
                            if (!window.confirm('Удалить продвижение?')) return;
                            try {
                              await adminApi.deletePromotion(p.id);
                              await loadPromotions();
                            } catch (err) {
                              setMsg(err.message || 'Ошибка');
                            }
                          }}
                        >
                          Удалить
                        </button>
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
            {promotions.length === 0 && (
              <p className="px-4 py-6 text-center text-gray-500 text-sm">Нет активных записей</p>
            )}
          </div>
        </div>
      )}
    </div>
  );
}
