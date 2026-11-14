import { Suspense, useEffect, useRef, useState } from "react";
import {
  NavLink,
  Link,
  Outlet,
  useLocation,
  useNavigate,
} from "react-router-dom";
import {
  Bell,
  ChevronDown,
  Compass,
  Flame,
  Zap,
  Grid2X2,
  Languages,
  LogOut,
  Play,
  Search,
  Settings,
  Shield,
  UserRound,
  X,
} from "lucide-react";
import { useMutation } from "@tanstack/react-query";
import {
  useApi,
  usePages,
  pageItems,
  queryClient,
} from "../shared/api/queries";
import { request } from "../shared/api/http";
import {
  Avatar,
  Button,
  Dialog,
  IconButton,
  LoadMore,
  Status,
} from "../shared/ui/Primitives";
import { useSession } from "../features/auth/Session";
import LoginDialog from "../features/auth/LoginDialog";
import { usePreferences, PreferencesProvider } from "../shared/preferences";
import { date, twitchProfilePath } from "../shared/format";
import { TWITCH_BROADCAST_LANGUAGES } from "../data/twitchLanguages";

const links = [
  { to: "/", label: "Обзор", icon: Compass },
  { to: "/trends", label: "В тренде", icon: Flame },
  { to: "/categories", label: "Категории", icon: Grid2X2 },
  { to: "/feed", label: "Смотреть", icon: Play },
  { to: "/cliprush", label: "ClipRush", icon: Zap },
];

export default function Shell() {
  return (
    <PreferencesProvider>
      <ShellContent />
    </PreferencesProvider>
  );
}

function ShellContent() {
  const session = useSession();
  const location = useLocation();
  const navigate = useNavigate();
  const search = useRef(null);
  const [loginOpen, setLoginOpen] = useState(false);
  const [languagesOpen, setLanguagesOpen] = useState(false);
  const [notificationsOpen, setNotificationsOpen] = useState(false);
  const config = useApi("/config", { staleTime: 300000 });
  const followed = useApi(session.user ? "/channels/followed" : null, {
    staleTime: 60000,
    refetchInterval: 60000,
  });
  const alerts = useApi(session.user ? "/notifications/unread-count" : null, {
    refetchInterval: 60000,
  });
  const { languages, toggleLanguage, resetLanguages } = usePreferences();
  const openLogin = () => setLoginOpen(true);
  useEffect(() => {
    setNotificationsOpen(false);
    document.getElementById("page-content")?.scrollTo(0, 0);
  }, [location.pathname]);
  useEffect(() => {
    const onKey = (event) => {
      if ((event.ctrlKey || event.metaKey) && event.key === "k") {
        event.preventDefault();
        search.current?.focus();
      }
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, []);
  const nav = (className) => (
    <nav className={className} aria-label="Основная навигация">
      {links.map(({ to, label, icon: Icon }) => (
        <NavLink end={to === "/"} key={to} to={to}>
          <Icon size={19} />
          <span>{label}</span>
        </NavLink>
      ))}
    </nav>
  );
  return (
    <div className="app-shell">
      <a href="#page-content" className="skip-link">
        К содержимому
      </a>
      <header className="app-header">
        <Link className="wordmark" to="/" aria-label="ClipStream — главная">
          <span className="brand-mark">
            <Play size={16} fill="currentColor" />
          </span>
          <span>
            clipstream<span className="brand-dot">.</span>
          </span>
        </Link>
        <form
          className="header-search"
          onSubmit={(event) => {
            event.preventDefault();
            const q = new FormData(event.currentTarget).get("q").trim();
            navigate(q ? `/?q=${encodeURIComponent(q)}` : "/");
          }}
        >
          <Search size={18} />
          <input
            ref={search}
            name="q"
            aria-label="Поиск клипов, каналов и людей"
            placeholder="Клипы, каналы, люди"
            type="search"
            autoComplete="off"
            enterKeyHint="search"
          />
          <kbd>⌘ K</kbd>
        </form>
        <div className="header-actions">
          <IconButton
            label="Языки клипов"
            aria-haspopup="dialog"
            onClick={() => setLanguagesOpen(true)}
          >
            <Languages size={19} />
            {languages.length > 0 && <span className="notification-dot" />}
          </IconButton>
          {session.authenticated ? (
            <>
              <IconButton
                label="Уведомления"
                aria-haspopup="dialog"
                onClick={() => setNotificationsOpen(true)}
              >
                <Bell size={19} />
                {alerts.data?.unread > 0 && (
                  <span className="notification-dot" />
                )}
              </IconButton>
              {session.user ? (
                <Link
                  className="profile-link"
                  to={`/u/${encodeURIComponent(session.user.username)}`}
                  aria-label="Мой профиль"
                >
                  <Avatar user={session.user} />
                </Link>
              ) : (
                <span
                  className="profile-link"
                  role="status"
                  aria-label="Загрузка профиля"
                >
                  <Avatar />
                </span>
              )}
            </>
          ) : (
            <Button variant="primary" onClick={openLogin}>
              <span className="login-label">Войти</span>
              <UserRound size={17} />
            </Button>
          )}
        </div>
      </header>
      <aside className="sidebar">
        {nav("side-navigation")}
        {session.user && (
          <div className="side-group">
            <p className="eyebrow">Моё пространство</p>
            <NavLink to={`/u/${encodeURIComponent(session.user.username)}`}>
              <UserRound size={18} /> Профиль
            </NavLink>
            <Link
              to={`/u/${encodeURIComponent(session.user.username)}?tab=settings`}
            >
              <Settings size={18} /> Настройки
            </Link>
            <NavLink to="/following">
              <Bell size={18} /> Отслеживаю
            </NavLink>
            {[1, 2].includes(session.user.role) && (
              <Link to="/staff">
                <Shield size={18} /> Консоль
              </Link>
            )}
          </div>
        )}
        <div className="side-group subscriptions">
          <div className="section-label">
            <p className="eyebrow">Подписки Twitch</p>
            <span className="muted">
              {followed.data?.channels?.length || ""}
            </span>
          </div>
          {session.user ? (
            <Status
              error={followed.error}
              loading={followed.isPending}
              onRetry={followed.refetch}
            >
              {(followed.data?.channels || []).slice(0, 12).map((channel) => (
                <Link
                  key={channel.broadcaster_id}
                  className="subscription"
                  to={twitchProfilePath(
                    channel.broadcaster_id,
                    channel.broadcaster_login,
                  )}
                >
                  <Avatar
                    user={{
                      avatar_url:
                        channel.profile_image_url || channel.avatar_url,
                      username: channel.broadcaster_name,
                    }}
                  />
                  <span>{channel.broadcaster_name}</span>
                  {channel.is_live && (
                    <span className="live-dot" title="В эфире" />
                  )}
                </Link>
              ))}
              {followed.data?.channels?.length === 0 && (
                <p className="side-note">
                  Ваши каналы появятся здесь после подключения Twitch.
                </p>
              )}
            </Status>
          ) : (
            <>
              <p className="side-note">
                Любимые каналы и рекомендации — после входа.
              </p>
              <button className="text-link" onClick={openLogin}>
                Подключить Twitch <ChevronDown size={13} />
              </button>
            </>
          )}
        </div>
        <div className="sidebar-footer">
          <Link to="/legal">Документы и контакты</Link>
          <p>Моменты остаются.</p>
          <small>© {new Date().getFullYear()} ClipStream</small>
        </div>
      </aside>
      <main id="page-content" className="main-content" tabIndex={-1}>
        {session.error && session.authenticated && (
          <div className="session-banner" role="status">
            Не удалось обновить профиль. Ваша сессия сохранена.{" "}
            <button onClick={() => session.refresh()}>Повторить</button>
          </div>
        )}
        <Suspense fallback={<Status loading />}>
          <Outlet
            context={{ openLogin, config: config.data, configQuery: config }}
          />
        </Suspense>
      </main>
      {nav("bottom-navigation")}
      <Dialog
        open={languagesOpen}
        onClose={() => setLanguagesOpen(false)}
        title="Языки клипов"
      >
        <p className="muted">
          Выберите языки, на которых вам интересно смотреть. Без выбора
          показываются все.
        </p>
        <div className="language-options">
          {TWITCH_BROADCAST_LANGUAGES.map(({ code, label }) => (
            <label key={code}>
              <input
                type="checkbox"
                checked={languages.includes(code)}
                onChange={() => toggleLanguage(code)}
              />
              {label}
              <span>{code.toUpperCase()}</span>
            </label>
          ))}
        </div>
        <div className="dialog-actions">
          <Button onClick={resetLanguages}>Все языки</Button>
          <Button variant="primary" onClick={() => setLanguagesOpen(false)}>
            Готово
          </Button>
        </div>
      </Dialog>
      <NotificationDialog
        open={notificationsOpen}
        onClose={() => setNotificationsOpen(false)}
      />
      <LoginDialog
        open={loginOpen}
        onClose={() => setLoginOpen(false)}
        config={config}
      />
    </div>
  );
}

function NotificationDialog({ open, onClose }) {
  const navigate = useNavigate();
  const data = usePages(
    open ? "/notifications" : null,
    "notifications",
    {},
    { pageSize: 30, staleTime: 0 },
  );
  const refresh = () =>
    queryClient.invalidateQueries({
      predicate: (query) =>
        String(query.queryKey[1]).startsWith("/notifications"),
    });
  const read = useMutation({
    mutationFn: (ids) =>
      request("/notifications/read", { method: "POST", body: { ids } }),
    onSuccess: refresh,
  });
  const all = useMutation({
    mutationFn: () =>
      request("/notifications/read-all", { method: "POST", body: {} }),
    onSuccess: refresh,
  });
  const rows = pageItems(data, "notifications");
  return (
    <Dialog open={open} onClose={onClose} title="Уведомления">
      <Status
        loading={data.isPending}
        error={data.error || all.error || read.error}
        onRetry={data.refetch}
      >
        {rows.length === 0 && (
          <p className="empty-state">Новых уведомлений пока нет.</p>
        )}
        <div className="notification-list">
          {rows.map((row) => (
            <button
              key={row.id}
              className={row.read ? "" : "unread"}
              onClick={async () => {
                const target = row.payload?.clip_id
                  ? `/clip/${row.payload.clip_id}`
                  : row.payload?.profile_username
                    ? `/u/${encodeURIComponent(row.payload.profile_username)}?tab=wall`
                    : row.actor_username
                      ? `/u/${encodeURIComponent(row.actor_username)}`
                      : "/";
                try {
                  await read.mutateAsync([row.id]);
                  onClose();
                  navigate(target);
                } catch {
                  /* mutation error is displayed */
                }
              }}
            >
              {row.actor_username || "Пользователь"}{" "}
              {{
                comment_reply: "ответил на ваш комментарий",
                friend_request: "хочет добавить вас в друзья",
                friend_accepted: "принял заявку в друзья",
                wall_mention: "упомянул вас в записи",
                new_follower: "подписался на вас",
              }[row.type] || "отправил уведомление"}
              <span className="muted">{date(row.created_at)}</span>
            </button>
          ))}
        </div>
        <LoadMore query={data} />
        {rows.length > 0 && (
          <Button busy={all.isPending} onClick={() => all.mutate()}>
            Прочитать всё
          </Button>
        )}
      </Status>
    </Dialog>
  );
}
