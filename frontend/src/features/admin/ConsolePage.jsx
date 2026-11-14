import { useState } from "react";
import { Link, useOutletContext, useSearchParams } from "react-router-dom";
import { useMutation } from "@tanstack/react-query";
import {
  Activity,
  ArrowUpRight,
  CheckCircle2,
  RefreshCw,
  Shield,
  Trash2,
} from "lucide-react";
import { request, queryString } from "../../shared/api/http";
import {
  useApi,
  usePages,
  pageItems,
  queryClient,
} from "../../shared/api/queries";
import {
  Avatar,
  Button,
  Dialog,
  LoadMore,
  PageHeading,
  Status,
} from "../../shared/ui/Primitives";
import { date, number } from "../../shared/format";
import { useSession } from "../auth/Session";
import AdsManager from "./AdsManager";
import ModerationManager from "./ModerationManager";

const roleNames = {
  user: "Пользователь",
  moderator: "Модератор",
  admin: "Администратор",
  vip: "VIP",
};
export default function ConsolePage({ admin = false }) {
  const session = useSession();
  const { openLogin } = useOutletContext();
  const [params, setParams] = useSearchParams();
  const allowed = admin
    ? session.user?.role === 2
    : [1, 2].includes(session.user?.role);
  const common = [
    ["moderation", "Ограничения"],
    ["reports", "Жалобы"],
    ["ip", "Блокировки IP"],
    ["audit", "Журнал"],
  ];
  const tabs = admin
    ? [
        ["overview", "Статистика"],
        ["users", "Пользователи"],
        ["ads", "Реклама"],
        ["promotions", "Продвижение клипов"],
        ["system", "Система"],
        ...common,
      ]
    : [["overview", "Обзор"], ["system", "Система"], ...common];
  const tab = tabs.some(([key]) => key === params.get("tab"))
    ? params.get("tab")
    : "overview";
  return (
    <div className="page console-page">
      <PageHeading
        eyebrow="Управление ClipStream"
        title={admin ? "Панель администратора" : "Консоль персонала"}
        description="Состояние сервиса, сообщество и управление контентом."
      >
        {session.user?.role === 2 && (
          <Link className="button" to={admin ? "/staff" : "/admin"}>
            {admin ? "Система" : "Администрирование"} <ArrowUpRight size={16} />
          </Link>
        )}
      </PageHeading>
      {!session.authenticated ? (
        <div className="surface">
          <p>Войдите, чтобы продолжить.</p>
          <Button onClick={openLogin}>Войти</Button>
        </div>
      ) : session.loading ? (
        <Status loading />
      ) : session.error ? (
        <Status error={session.error} onRetry={session.refresh} />
      ) : !allowed ? (
        <Status
          error={
            new Error(
              "Этот раздел доступен только персоналу с соответствующими правами.",
            )
          }
        />
      ) : (
        <>
          <nav className="profile-tabs" aria-label="Разделы управления">
            {tabs.map(([key, label]) => (
              <button
                key={key}
                aria-current={tab === key ? "page" : undefined}
                onClick={() => setParams({ tab: key })}
              >
                {label}
              </button>
            ))}
          </nav>
          {tab === "overview" && <Overview admin={admin} />}
          {tab === "system" && <System />}
          {tab === "users" && <UserManagement />}
          {tab === "promotions" && <Promotions />}
          {tab === "ads" && <AdsManager />}
          {["moderation", "reports", "ip", "audit"].includes(tab) && (
            <ModerationManager
              section={tab === "moderation" ? "accounts" : tab}
            />
          )}
        </>
      )}
    </div>
  );
}

function Overview({ admin }) {
  const data = useApi(admin ? "/admin/dashboard" : "/staff/bootstrap", {
    staleTime: 10000,
  });
  const metrics = data.data?.overview;
  return (
    <>
      <div className="section-label">
        <h2>{admin ? "Платформа сегодня" : "Рабочее пространство"}</h2>
        <Button busy={data.isFetching} onClick={() => data.refetch()}>
          <RefreshCw size={15} /> Обновить
        </Button>
      </div>
      <Status
        loading={data.isPending}
        error={data.error}
        onRetry={data.refetch}
      >
        {admin ? (
          <>
            <div className="metric-grid">
              {[
                ["users_total", "Пользователей"],
                ["clips_total", "Клипов"],
                ["comments_total", "Комментариев"],
                ["views_24h", "Открытий за сутки"],
                ["users_admin", "Администраторов"],
                ["users_moderator", "Модераторов"],
              ].map(([key, label]) => (
                <div className="surface metric" key={key}>
                  <span>{label}</span>
                  <strong>{number(metrics?.[key])}</strong>
                </div>
              ))}
            </div>
            <section className="surface">
              <h2>Активность за 24 часа</h2>
              <div className="metric-grid">
                <div className="metric">
                  <span>Активных сессий</span>
                  <strong>
                    {number(data.data?.analytics_24h?.sessions_touched_24h)}
                  </strong>
                </div>
                <div className="metric">
                  <span>Секунд из клиентских событий за 7 дней</span>
                  <strong>
                    {number(data.data?.analytics_24h?.user_daily_7d?.watch_sec)}
                  </strong>
                </div>
              </div>
              <p className="muted">
                Открытия учитываются отдельно. Плеер клипов Twitch не передаёт
                достоверное время воспроизведения; новый интерфейс не отправляет
                вымышленные секунды просмотра.
              </p>
            </section>
          </>
        ) : (
          <div className="surface">
            <h2>{data.data?.username}</h2>
            <p className="muted">
              Роль: {roleNames[data.data?.role] || "Персонал"}
            </p>
            <Link className="text-link" to="/staff?tab=system">
              Проверить состояние системы <ArrowUpRight size={16} />
            </Link>
          </div>
        )}
      </Status>
      {!admin && <System />}
    </>
  );
}

function System() {
  const query = useApi("/staff/diagnostics", { refetchInterval: 30000 });
  const eventsub = useApi("/eventsub/status", { refetchInterval: 30000 });
  return (
    <section>
      <div className="section-label">
        <h2>Состояние компонентов</h2>
        <Button
          busy={query.isFetching}
          onClick={() => {
            query.refetch();
            eventsub.refetch();
          }}
        >
          Обновить
        </Button>
      </div>
      <Status
        loading={query.isPending}
        error={query.error}
        onRetry={query.refetch}
      >
        <div className="metric-grid">
          {[
            ["MySQL", query.data?.mysql_ok],
            ["Redis", query.data?.redis_ok],
          ].map(([label, ok]) => (
            <div className="surface component-health" key={label}>
              <span>{label}</span>
              <strong className={ok ? "healthy" : "unhealthy"}>
                {ok ? <CheckCircle2 size={18} /> : <Activity size={18} />}
                {ok ? "Доступен" : "Недоступен"}
              </strong>
            </div>
          ))}
          <div className="surface component-health">
            <span>Время работы API</span>
            <strong>
              {Math.floor((query.data?.uptime_seconds || 0) / 60)} мин.
            </strong>
          </div>
        </div>
      </Status>
      <div className="surface">
        <h3>События Twitch</h3>
        <Status
          loading={eventsub.isPending}
          error={eventsub.error}
          onRetry={eventsub.refetch}
        >
          <p>
            {eventsub.data?.eventsub?.enabled
              ? eventsub.data.eventsub.connected
                ? eventsub.data.eventsub.subscribed
                  ? "Подписки активны"
                  : "Соединение установлено, подписки настраиваются"
                : "Ожидание соединения"
              : "Обработчик событий выключен"}
          </p>
          <p className="muted">
            {eventsub.data?.eventsub?.last_error ||
              "Статус обновляется автоматически."}
          </p>
        </Status>
      </div>
      <p className="muted">
        Версия {query.data?.process?.version || "—"} · обновлено{" "}
        {query.data?.utc_time
          ? new Date(query.data.utc_time).toLocaleString("ru")
          : "—"}
      </p>
    </section>
  );
}

function UserManagement() {
  const [search, setSearch] = useState("");
  const query = usePages(
    "/admin/users",
    "users",
    { q: search },
    { pageSize: 30 },
  );
  const rows = pageItems(query, "users");
  return (
    <section>
      <form
        className="inline-form"
        onSubmit={(event) => {
          event.preventDefault();
          setSearch(new FormData(event.currentTarget).get("q").trim());
        }}
      >
        <input
          name="q"
          aria-label="Найти пользователя"
          placeholder="Имя или почта"
          type="search"
        />
        <Button>Найти</Button>
      </form>
      <Status
        loading={query.isPending}
        error={!query.data && query.error}
        onRetry={query.refetch}
      >
        <div className="table-scroll">
          <table>
            <thead>
              <tr>
                <th>Пользователь</th>
                <th>Почта</th>
                <th>Роль</th>
                <th>Действия</th>
              </tr>
            </thead>
            <tbody>
              {rows.map((user) => (
                <UserRole key={`${user.id}:${user.role}`} user={user} />
              ))}
            </tbody>
          </table>
        </div>
        {rows.length === 0 && (
          <p className="empty-state">Пользователи не найдены.</p>
        )}
        <LoadMore query={query} />
      </Status>
    </section>
  );
}

function UserRole({ user }) {
  const [role, setRole] = useState(user.role);
  const [confirm, setConfirm] = useState(false);
  const mutation = useMutation({
    mutationFn: () =>
      request(`/admin/users/${user.id}/role`, {
        method: "POST",
        body: { role },
      }),
    onSuccess: () => {
      setConfirm(false);
      queryClient.invalidateQueries({ queryKey: ["pages", "/admin/users"] });
      queryClient.invalidateQueries({ queryKey: ["api", "/admin/dashboard"] });
    },
  });
  return (
    <tr>
      <td>
        <Link
          className="creator"
          to={`/u/${encodeURIComponent(user.username)}`}
        >
          <Avatar user={user} />
          {user.username}
        </Link>
      </td>
      <td>{user.email}</td>
      <td>
        <select
          aria-label={`Роль ${user.username}`}
          value={role}
          onChange={(event) => setRole(event.target.value)}
        >
          {Object.entries(roleNames).map(([key, label]) => (
            <option key={key} value={key}>
              {label}
            </option>
          ))}
        </select>
      </td>
      <td>
        <Button disabled={role === user.role} onClick={() => setConfirm(true)}>
          Изменить
        </Button>
        <Dialog
          open={confirm}
          onClose={() => setConfirm(false)}
          title="Изменить права доступа"
        >
          <p>
            {user.username}: {roleNames[user.role]} → {roleNames[role]}
          </p>
          {mutation.error && <Status error={mutation.error} />}
          <div className="dialog-actions">
            <Button onClick={() => setConfirm(false)}>Отмена</Button>
            <Button
              variant="primary"
              busy={mutation.isPending}
              onClick={() => mutation.mutate()}
            >
              Подтвердить
            </Button>
          </div>
        </Dialog>
      </td>
    </tr>
  );
}

function Promotions() {
  const query = usePages(
    "/admin/promotions",
    "promotions",
    {},
    { pageSize: 30 },
  );
  const [deleting, setDeleting] = useState(null);
  const mutation = useMutation({
    mutationFn: ({ method, body, id }) =>
      request(`/admin/promotions${id ? "/" + id : ""}`, { method, body }),
    onSuccess: () => {
      setDeleting(null);
      queryClient.invalidateQueries({
        queryKey: ["pages", "/admin/promotions"],
      });
    },
  });
  const rows = pageItems(query, "promotions");
  const toUtc = (value) =>
    value ? new Date(value).toISOString().slice(0, 19).replace("T", " ") : "";
  return (
    <section>
      <h2>Продвижение клипов</h2>
      <form
        className="surface form-grid"
        onSubmit={(event) => {
          event.preventDefault();
          const form = Object.fromEntries(new FormData(event.currentTarget));
          mutation.mutate({
            method: "POST",
            body: {
              ...form,
              clip_id: Number(form.clip_id),
              boost_score: Number(form.boost_score),
              starts_at: toUtc(form.starts_at),
              ends_at: toUtc(form.ends_at),
            },
          });
        }}
      >
        <label>
          ID клипа
          <input name="clip_id" type="number" min={1} required />
        </label>
        <label>
          Вес продвижения
          <input
            name="boost_score"
            type="number"
            min={0}
            max={500}
            defaultValue={45}
            required
          />
        </label>
        <label>
          Размещение
          <select name="placement">
            {[
              ["recommended", "Рекомендации"],
              ["all", "Везде"],
              ["hot", "Популярное"],
              ["fresh", "Новинки"],
              ["feed", "Лента"],
              ["explore", "Открытия"],
              ["related", "Похожие"],
            ].map(([value, title]) => (
              <option key={value} value={value}>
                {title}
              </option>
            ))}
          </select>
        </label>
        <label>
          Метка
          <input name="label" maxLength={64} />
        </label>
        <label>
          Начало (местное время)
          <input name="starts_at" type="datetime-local" />
        </label>
        <label>
          Окончание (местное время)
          <input name="ends_at" type="datetime-local" />
        </label>
        <Button variant="primary" busy={mutation.isPending}>
          Добавить продвижение
        </Button>
      </form>
      {mutation.error && <Status error={mutation.error} />}
      {mutation.isSuccess && (
        <p className="feedback" role="status">
          Изменения сохранены
        </p>
      )}
      <Status
        loading={query.isPending}
        error={!query.data && query.error}
        onRetry={query.refetch}
      >
        <div className="table-scroll">
          <table>
            <thead>
              <tr>
                <th>Клип</th>
                <th>Размещение</th>
                <th>Вес</th>
                <th>Метка</th>
                <th>Окончание</th>
                <th />
              </tr>
            </thead>
            <tbody>
              {rows.map((row) => (
                <tr key={row.id}>
                  <td>
                    <Link to={`/clip/${row.clip_id}`}>#{row.clip_id}</Link>
                  </td>
                  <td>{row.placement}</td>
                  <td>{row.boost_score}</td>
                  <td>{row.label}</td>
                  <td>{date(row.ends_at, true) || "Без срока"}</td>
                  <td>
                    <Button onClick={() => setDeleting(row)}>
                      <Trash2 size={15} /> Удалить
                    </Button>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
        <LoadMore query={query} />
      </Status>
      <Dialog
        open={Boolean(deleting)}
        onClose={() => setDeleting(null)}
        title="Удалить продвижение"
      >
        <p>Клип останется на сайте, его дополнительный вес будет снят.</p>
        <Button
          variant="primary"
          busy={mutation.isPending}
          onClick={() => mutation.mutate({ method: "DELETE", id: deleting.id })}
        >
          Удалить
        </Button>
      </Dialog>
    </section>
  );
}
