import { Suspense, useEffect, useRef, useState } from "react";
import { isKeyboardNavigation, isSearchShortcut } from "../shared/input";
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
  Moon,
  Sun,
  LogOut,
  Play,
  Menu,
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

import useSidebar from "./useSidebar";
import brandMark from "../assets/clipstream-raccoon.svg?no-inline";

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
  const sidebar = useSidebar(location.pathname);
  const search = useRef(null);
  const [loginOpen, setLoginOpen] = useState(false);
  const [languagesOpen, setLanguagesOpen] = useState(false);
  const [languageSearch, setLanguageSearch] = useState("");
  const [notificationsOpen, setNotificationsOpen] = useState(false);
  const config = useApi("/config", { staleTime: 300000 });
  const followed = useApi(session.user ? "/channels/followed" : null, {
    staleTime: 60000,
    refetchInterval: 60000,
  });
  const alerts = useApi(session.user ? "/notifications/unread-count" : null, {
    refetchInterval: 60000,
  });
  const { languages, toggleLanguage, resetLanguages, theme, toggleTheme } =
    usePreferences();
  const selectedLanguages = TWITCH_BROADCAST_LANGUAGES.filter(({ code }) =>
    languages.includes(code),
  );
  const languageQuery = languageSearch.trim().toLocaleLowerCase();
  const visibleLanguages = TWITCH_BROADCAST_LANGUAGES.filter(
    ({ code, label }) =>
      `${label} ${code}`.toLocaleLowerCase().includes(languageQuery),
  );
  const openLogin = () => {
    sidebar.setOpen(false);
    setLoginOpen(true);
  };
  useEffect(() => {
    setNotificationsOpen(false);
    if (!location.hash) document.getElementById("page-content")?.scrollTo(0, 0);
  }, [location.pathname]);
  useEffect(() => {
    const keyboard = (event) => {
      if (isKeyboardNavigation(event))
        document.documentElement.dataset.input = "keyboard";
    };
    const pointer = () => {
      document.documentElement.dataset.input = "pointer";
    };
    document.addEventListener("keydown", keyboard, true);
    document.addEventListener("pointerdown", pointer, true);
    return () => {
      document.removeEventListener("keydown", keyboard, true);
      document.removeEventListener("pointerdown", pointer, true);
    };
  }, []);
  useEffect(() => {
    const onKey = (event) => {
      const platform = navigator.userAgentData?.platform || navigator.platform;
      if (
        isSearchShortcut(event, platform) &&
        !document.querySelector("dialog[open]")
      ) {
        event.preventDefault();
        document.documentElement.dataset.input = "keyboard";
        search.current?.focus();
      }
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, []);
  const nav = (className) => (
    <nav className={className} aria-label="Основная навигация">
      {links.map(({ to, label, icon: Icon }) => (
        <NavLink
          end={to === "/"}
          key={to}
          to={to}
          title={label}
          aria-label={label}
        >
          <Icon size={19} />
          <span>{label}</span>
        </NavLink>
      ))}
    </nav>
  );
  const sidebarContent = (
    <div className="sidebar-content" {...sidebar.gestures}>
      {nav("side-navigation")}
      <div
        className="sidebar-details"
        inert={!sidebar.mobile && sidebar.compact ? "" : undefined}
        aria-hidden={(!sidebar.mobile && sidebar.compact) || undefined}
      >
        <div className="sidebar-details-content">
          {session.user && (
            <div className="side-group">
              <p className="eyebrow">Моё пространство</p>
              <NavLink
                to={`/u/${encodeURIComponent(session.user.username)}`}
                title="Профиль"
                aria-label="Профиль"
              >
                <UserRound size={18} /> <span>Профиль</span>
              </NavLink>
              <Link
                to={`/u/${encodeURIComponent(session.user.username)}?tab=settings`}
                title="Настройки"
                aria-label="Настройки"
              >
                <Settings size={18} /> <span>Настройки</span>
              </Link>
              <NavLink
                to="/following"
                title="Отслеживаю"
                aria-label="Отслеживаю"
              >
                <Bell size={18} /> <span>Отслеживаю</span>
              </NavLink>
              {[1, 2].includes(session.user.role) && (
                <Link to="/staff" title="Консоль" aria-label="Консоль">
                  <Shield size={18} /> <span>Консоль</span>
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
                    title={channel.broadcaster_name}
                    aria-label={`${channel.broadcaster_name}${channel.is_live ? " — в эфире" : ""}`}
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
            <p>Стримы заканчиваются - моменты остаются</p>
            <small>© {new Date().getFullYear()} ClipStream</small>
          </div>
        </div>
      </div>
    </div>
  );
  return (
    <div
      className={`app-shell${!sidebar.mobile && sidebar.compact ? " sidebar-compact" : ""}`}
    >
      <a href="#page-content" className="skip-link">
        К содержимому
      </a>
      <header className="app-header">
        <div className="header-brand">
          <IconButton
            className="icon-button sidebar-toggle"
            label={
              sidebar.expanded
                ? "Свернуть боковую панель"
                : "Развернуть боковую панель"
            }
            aria-controls="app-sidebar"
            aria-expanded={sidebar.expanded}
            onClick={() => sidebar.setExpanded(!sidebar.expanded)}
          >
            <Menu size={24} strokeWidth={1.75} />
          </IconButton>
          <Link className="wordmark" to="/" aria-label="ClipStream — главная">
            <img
              className="brand-mark"
              src={brandMark}
              width="36"
              height="36"
              alt=""
            />
            <span>ClipStream</span>
          </Link>
        </div>
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
        </form>
        <div className="header-actions">
          <Button
            className="clip-language-trigger"
            aria-label="Языки клипов"
            aria-haspopup="dialog"
            onClick={() => {
              setLanguageSearch("");
              setLanguagesOpen(true);
            }}
          >
            <Languages size={19} />
            <span className="clip-language-label">Языки клипов</span>
            <span className="clip-language-value">
              {languages.length || "Все"}
            </span>
          </Button>
          <IconButton
            label={
              theme === "dark"
                ? "Включить светлую тему"
                : "Включить тёмную тему"
            }
            onClick={toggleTheme}
          >
            {theme === "dark" ? <Sun size={19} /> : <Moon size={19} />}
          </IconButton>
          {session.authenticated ? (
            <>
              <IconButton
                id="notifications-toggle"
                label="Уведомления"
                aria-controls="notifications-panel"
                aria-expanded={notificationsOpen}
                onClick={() => setNotificationsOpen((open) => !open)}
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
      {sidebar.mobile ? (
        <>
          <div
            className="sidebar-swipe-edge"
            {...sidebar.gestures}
            aria-hidden="true"
          />
          <Dialog
            open={sidebar.open}
            onClose={() => sidebar.setOpen(false)}
            title="Навигация"
            className="sidebar-drawer"
          >
            <aside
              id="app-sidebar"
              aria-label="Боковая панель"
              onClick={(event) => {
                if (event.target.closest("a")) sidebar.setOpen(false);
              }}
            >
              {sidebarContent}
            </aside>
          </Dialog>
        </>
      ) : (
        <aside id="app-sidebar" className="sidebar" aria-label="Боковая панель">
          {sidebarContent}
        </aside>
      )}
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
          Фильтр по языку речи в клипах Twitch. Можно выбрать несколько языков.
          Язык интерфейса не меняется. Без выбора показываются все клипы.
        </p>
        <label className="language-search">
          Найти язык
          <input
            type="search"
            value={languageSearch}
            onChange={(event) => setLanguageSearch(event.target.value)}
            placeholder="Название или код: Русский, EN…"
          />
        </label>
        <div className="language-selection">
          <p className="muted" role="status">
            {languages.length
              ? `Выбрано: ${languages.length}`
              : "Показываются клипы на всех языках"}
          </p>
          {selectedLanguages.length > 0 && (
            <div className="language-chips">
              {selectedLanguages.map(({ code, label }) => (
                <button
                  key={code}
                  onClick={() => toggleLanguage(code)}
                  aria-label={`Убрать язык: ${label}`}
                >
                  {label}
                  <X size={14} />
                </button>
              ))}
            </div>
          )}
        </div>
        <div className="language-options">
          {visibleLanguages.map(({ code, label }) => (
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
        {visibleLanguages.length === 0 && (
          <p className="muted">
            Язык не найден. Попробуйте другое название или код.
          </p>
        )}
        <div className="dialog-actions">
          <Button onClick={resetLanguages}>Все языки</Button>
          <Button variant="primary" onClick={() => setLanguagesOpen(false)}>
            Готово
          </Button>
        </div>
      </Dialog>
      <NotificationPanel
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

function NotificationPanel({ open, onClose }) {
  const navigate = useNavigate();
  const panel = useRef(null);
  const close = useRef(onClose);
  close.current = () => {
    if (panel.current?.contains(document.activeElement))
      document.getElementById("notifications-toggle")?.focus();
    onClose();
  };
  useEffect(() => {
    if (!open) return;
    const element = panel.current;
    const trigger = document.getElementById("notifications-toggle");
    const dismiss = (event) => {
      if (!element.contains(event.target) && !trigger?.contains(event.target))
        close.current();
    };
    const onKey = (event) => {
      if (event.key === "Escape" && !event.defaultPrevented) {
        event.preventDefault();
        close.current();
      }
    };
    element.focus();
    document.addEventListener("click", dismiss, true);
    document.addEventListener("keydown", onKey);
    return () => {
      document.removeEventListener("click", dismiss, true);
      document.removeEventListener("keydown", onKey);
      if (element.contains(document.activeElement)) trigger?.focus();
    };
  }, [open]);
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
  if (!open) return null;
  return (
    <section
      ref={panel}
      id="notifications-panel"
      className="notifications-panel"
      aria-labelledby="notifications-title"
      tabIndex={-1}
    >
      <div className="notifications-heading">
        <h2 id="notifications-title">Уведомления</h2>
        <IconButton label="Закрыть уведомления" onClick={() => close.current()}>
          <X size={20} />
        </IconButton>
      </div>
      <div className="notifications-body">
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
      </div>
    </section>
  );
}
