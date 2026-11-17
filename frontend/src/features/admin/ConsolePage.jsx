import ConfirmDialog from "../../shared/ui/Confirmation";
import SectionNav from "../../shared/ui/SectionNav";
import DataTable from "../../shared/ui/DataTable";
import { Metric, MetricGrid } from "../../shared/ui/Metrics";
import { Surface, SectionHeading } from "../../shared/ui/Layout";
import { Feedback, EmptyState } from "../../shared/ui/Feedback";
import { Input, Field } from "../../shared/ui/FormControls";
import Select from "../../shared/ui/Select";
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
  LayoutDashboard,
  Users,
  Flag,
  Database,
  Megaphone,
  Rocket,
  Server,
  Network,
  History,
} from "lucide-react";
import { request } from "../../shared/api/http";
import {
  useApi,
  usePages,
  pageItems,
  queryClient,
} from "../../shared/api/queries";
import {
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
import ImportMetrics from "./ImportMetrics";
import UsersManager from "./UsersManager";
import { BarChart } from "./ConsoleCharts";
import "./Console.css";

export default function ConsolePage({ admin: adminRoute = false }) {
  const session = useSession();
  const admin = adminRoute || session.user?.role === 2;
  const { openLogin } = useOutletContext();
  const [params, setParams] = useSearchParams();
  const allowed = admin
    ? session.user?.role === 2
    : [1, 2].includes(session.user?.role);
  const tabs = [
    ["overview", "Обзор", LayoutDashboard],
    ["users", "Пользователи и доступ", Users],
    ["reports", "Жалобы", Flag],
    ...(admin
      ? [
          ["import", "Каталог и импорт", Database],
          ["ads", "Реклама", Megaphone],
          ["promotions", "Продвижение клипов", Rocket],
        ]
      : []),
    ["system", "Система", Server],
    ["ip", "Блокировки IP", Network],
    ["audit", "Журнал", History],
  ];
  const requestedTab =
    params.get("tab") === "moderation" ? "users" : params.get("tab");
  const tab = tabs.some(([key]) => key === requestedTab)
    ? requestedTab
    : "overview";
  return (
    <div className="page console-page">
      <PageHeading
        eyebrow="Управление ClipStream"
        title={admin ? "Панель администратора" : "Консоль персонала"}
        description="Сообщество, модерация и работа платформы в одном пространстве."
      />
      {!session.authenticated ? (
        <Surface>
          <p>Войдите, чтобы продолжить.</p>
          <Button onClick={openLogin}>Войти</Button>
        </Surface>
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
          <div className="console-layout">
            <SectionNav
              label="Разделы управления"
              items={tabs}
              value={tab}
              onChange={(key) => setParams({ tab: key })}
              rail
            />
            <div className="console-content" key={tab}>
              {tab === "overview" && <Overview admin={admin} />}
              {tab === "system" && <System />}
              {tab === "users" && <UsersManager />}
              {tab === "import" && <ImportPanel />}
              {tab === "promotions" && <Promotions />}
              {tab === "ads" && <AdsManager />}
              {["reports", "ip", "audit"].includes(tab) && (
                <ModerationManager section={tab} />
              )}
            </div>
          </div>
        </>
      )}
    </div>
  );
}

function Overview({ admin }) {
  const data = useApi(admin ? "/admin/dashboard" : "/staff/bootstrap", {
    staleTime: 10000,
    refetchInterval: 30000,
  });
  const metrics = data.data?.overview;
  return (
    <>
      <SectionHeading>
        <h2>{admin ? "Платформа сегодня" : "Рабочее пространство"}</h2>
        <Button busy={data.isFetching} onClick={() => data.refetch()}>
          <RefreshCw size={15} /> Обновить
        </Button>
      </SectionHeading>
      <Status
        loading={data.isPending}
        error={data.error}
        onRetry={data.refetch}
      >
        {admin ? (
          <>
            <MetricGrid>
              {[
                ["users_total", "Пользователей"],
                ["clips_total", "Клипов"],
                ["comments_total", "Комментариев"],
                ["views_24h", "Открытий за сутки"],
              ].map(([key, label]) => (
                <Metric
                  key={key}
                  label={<>{label}</>}
                  value={<>{number(metrics?.[key])}</>}
                />
              ))}
            </MetricGrid>
            <div className="console-chart-grid">
              <BarChart
                title="Состав сообщества"
                description="Текущее распределение ролей"
                rows={[
                  ["Пользователи", metrics?.users_regular],
                  ["VIP", metrics?.users_vip],
                  ["Модераторы", metrics?.users_moderator],
                  ["Администраторы", metrics?.users_admin],
                ]}
              />
              <BarChart
                title="Категории каталога"
                description="Крупнейшие категории по количеству клипов"
                rows={(data.data?.clip_import?.categories || [])
                  .slice(0, 6)
                  .map((row) => [row.name, row.clips])}
              />
            </div>
            <div className="console-shortcuts">
              <Surface as={Link} to="?tab=users">
                <Shield size={20} />
                <strong>Пользователи и доступ</strong>
                <span className="muted">Роли, блокировки, отстранения</span>
              </Surface>
              <Surface as={Link} to="?tab=reports">
                <Activity size={20} />
                <strong>Очередь жалоб</strong>
                <span className="muted">Решения по контенту и сообществу</span>
              </Surface>
              <Surface as={Link} to="?tab=import">
                <RefreshCw size={20} />
                <strong>Каталог и импорт</strong>
                <span className="muted">Динамика загрузки и бюджет Twitch</span>
              </Surface>
            </div>
            <Surface as="section">
              <h2>Активность за 24 часа</h2>
              <MetricGrid>
                <Metric
                  label={<>Активных сессий</>}
                  value={
                    <>
                      {number(data.data?.analytics_24h?.sessions_touched_24h)}
                    </>
                  }
                  surface={false}
                />
                <Metric
                  label={<>Секунд из клиентских событий за 7 дней</>}
                  value={
                    <>
                      {number(
                        data.data?.analytics_24h?.user_daily_7d?.watch_sec,
                      )}
                    </>
                  }
                  surface={false}
                />
              </MetricGrid>
              <p className="muted">
                Открытия учитываются отдельно. Плеер клипов Twitch не передаёт
                достоверное время воспроизведения; новый интерфейс не отправляет
                вымышленные секунды просмотра.
              </p>
            </Surface>
          </>
        ) : (
          <Surface>
            <h2>{data.data?.username}</h2>
            <p className="muted">
              Роль:{" "}
              {{ admin: "Администратор", moderator: "Модератор" }[
                data.data?.role
              ] || "Персонал"}
            </p>
            <Link className="text-link" to="/staff?tab=system">
              Проверить состояние системы <ArrowUpRight size={16} />
            </Link>
          </Surface>
        )}
      </Status>
      {!admin && <System />}
    </>
  );
}

function ImportPanel() {
  const query = useApi("/admin/dashboard", { refetchInterval: 30000 });
  return (
    <Status
      loading={query.isPending}
      error={query.error}
      onRetry={query.refetch}
    >
      <ImportMetrics data={query.data?.clip_import} />
    </Status>
  );
}

function System() {
  const query = useApi("/staff/diagnostics", { refetchInterval: 30000 });
  const eventsub = useApi("/eventsub/status", { refetchInterval: 30000 });
  return (
    <section>
      <SectionHeading>
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
      </SectionHeading>
      <Status
        loading={query.isPending}
        error={query.error}
        onRetry={query.refetch}
      >
        <MetricGrid>
          {[
            ["MySQL", query.data?.mysql_ok],
            ["Redis", query.data?.redis_ok],
          ].map(([label, ok]) => (
            <Surface className="component-health" key={label}>
              <span>{label}</span>
              <strong className={ok ? "healthy" : "unhealthy"}>
                {ok ? <CheckCircle2 size={18} /> : <Activity size={18} />}
                {ok ? "Доступен" : "Недоступен"}
              </strong>
            </Surface>
          ))}
          <Surface className="component-health">
            <span>Время работы API</span>
            <strong>
              {Math.floor((query.data?.uptime_seconds || 0) / 60)} мин.
            </strong>
          </Surface>
        </MetricGrid>
      </Status>
      <Surface>
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
      </Surface>
      <p className="muted">
        Версия {query.data?.process?.version || "—"} · обновлено{" "}
        {query.data?.utc_time
          ? new Date(query.data.utc_time).toLocaleString("ru")
          : "—"}
      </p>
    </section>
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
  const [creating, setCreating] = useState(false);
  const mutation = useMutation({
    mutationFn: ({ method, body, id }) =>
      request(`/admin/promotions${id ? "/" + id : ""}`, { method, body }),
    onSuccess: () => {
      setDeleting(null);
      setCreating(false);
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
      <SectionHeading>
        <div>
          <h2>Продвижение клипов</h2>
          <p className="muted">
            Дополнительный вес в выбранных лентах на заданный период.
          </p>
        </div>
        <Button
          variant="primary"
          onClick={() => {
            mutation.reset();
            setCreating(true);
          }}
        >
          Добавить продвижение
        </Button>
      </SectionHeading>
      <Dialog
        open={creating}
        onClose={() => {
          if (!mutation.isPending) setCreating(false);
        }}
        title="Новое продвижение"
      >
        <Surface
          as="form"
          className="form-grid"
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
          <Field>
            ID клипа
            <Input name="clip_id" type="number" min={1} required />
          </Field>
          <Field>
            Вес продвижения
            <Input
              name="boost_score"
              type="number"
              min={0}
              max={500}
              defaultValue={45}
              required
            />
          </Field>
          <Field>
            Размещение
            <Select aria-label="Размещение" name="placement">
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
            </Select>
          </Field>
          <Field>
            Метка
            <Input name="label" maxLength={64} />
          </Field>
          <Field>
            Начало (местное время)
            <Input name="starts_at" type="datetime-local" />
          </Field>
          <Field>
            Окончание (местное время)
            <Input name="ends_at" type="datetime-local" />
          </Field>
          <Button variant="primary" busy={mutation.isPending}>
            Добавить продвижение
          </Button>
        </Surface>
        {mutation.error && <Status error={mutation.error} />}
      </Dialog>
      {mutation.isSuccess && (
        <Feedback role="status">Изменения сохранены</Feedback>
      )}
      <Status
        loading={query.isPending}
        error={!query.data && query.error}
        onRetry={query.refetch}
      >
        <DataTable label="Продвижение клипов">
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
                  <Button
                    size="compact"
                    onClick={() => {
                      mutation.reset();
                      setDeleting(row);
                    }}
                  >
                    <Trash2 size={15} /> Удалить
                  </Button>
                </td>
              </tr>
            ))}
          </tbody>
        </DataTable>

        {rows.length === 0 && (
          <EmptyState as="p">Продвижений пока нет.</EmptyState>
        )}
        <LoadMore query={query} />
      </Status>
      <ConfirmDialog
        open={Boolean(deleting)}
        onClose={() => setDeleting(null)}
        title="Удалить продвижение"
        pending={mutation.isPending}
        onConfirm={() => mutation.mutate({ method: "DELETE", id: deleting.id })}
        confirmLabel={<>Удалить</>}
        error={mutation.error}
      >
        <p>Клип останется на сайте, его дополнительный вес будет снят.</p>
      </ConfirmDialog>
    </section>
  );
}
