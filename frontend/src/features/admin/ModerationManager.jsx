import Select from "../../shared/ui/Select";
import { useState } from "react";
import { Link } from "react-router-dom";
import { useMutation } from "@tanstack/react-query";
import { Shield, Search, Trash2 } from "lucide-react";
import { request } from "../../shared/api/http";
import {
  useApi,
  usePages,
  pageItems,
  queryClient,
} from "../../shared/api/queries";
import { Button, Dialog, LoadMore, Status } from "../../shared/ui/Primitives";
import { date } from "../../shared/format";
export default function ModerationManager({ section = "accounts" }) {
  return section === "ip" ? (
    <IpBans />
  ) : section === "audit" ? (
    <Audit />
  ) : section === "reports" ? (
    <Reports />
  ) : (
    <Accounts />
  );
}
function Accounts() {
  const [id, setId] = useState("");
  const state = useApi(id ? `/moderation/users/${id}` : null);
  const [pending, setPending] = useState(null);
  const mutation = useMutation({
    mutationFn: ({ id: target, ...body }) =>
      request(`/moderation/users/${target}/restriction`, {
        method: "POST",
        body,
      }),
    onSuccess: () => {
      setPending(null);
      state.refetch();
      queryClient.invalidateQueries({ queryKey: ["pages", "/admin/users"] });
    },
  });
  return (
    <section>
      <h2>Доступ и ограничения</h2>
      <p className="muted">
        Временное отстранение или бан закрывает вход и отзывает действующие
        сессии. Ограничение обсуждений сохраняет доступ к просмотру.
      </p>
      <form
        className="inline-form"
        onSubmit={(event) => {
          event.preventDefault();
          setId(new FormData(event.currentTarget).get("id"));
        }}
      >
        <input
          name="id"
          type="number"
          min={1}
          placeholder="ID пользователя"
          aria-label="ID пользователя"
          required
        />
        <Button>
          <Search size={15} /> Проверить
        </Button>
      </form>
      {id && (
        <Status
          loading={state.isPending}
          error={state.error}
          onRetry={state.refetch}
        >
          {state.data && (
            <div className="surface">
              <h3>
                {state.data.username} · #{state.data.id}
              </h3>
              <p>
                {state.data.account_blocked
                  ? "Постоянная блокировка"
                  : state.data.account_suspended_until &&
                      new Date(
                        state.data.account_suspended_until.replace(" ", "T") +
                          "Z",
                      ) > new Date()
                    ? `Отстранён до ${date(state.data.account_suspended_until, true)}`
                    : "Аккаунт активен"}
              </p>
              {state.data.account_ban_reason && (
                <p className="muted">{state.data.account_ban_reason}</p>
              )}
              {state.data.chat_banned_permanently ? (
                <p>Обсуждения ограничены бессрочно</p>
              ) : (
                state.data.chat_banned_until && (
                  <p>
                    Ограничение обсуждений до{" "}
                    {date(state.data.chat_banned_until, true)}
                  </p>
                )
              )}
            </div>
          )}
        </Status>
      )}
      <form
        className="surface form-grid"
        onSubmit={(event) => {
          event.preventDefault();
          const form = Object.fromEntries(new FormData(event.currentTarget));
          setPending({
            ...form,
            id: Number(form.id),
            hours: Number(form.hours),
            permanent: Boolean(form.permanent),
          });
        }}
      >
        <label>
          ID пользователя
          <input
            name="id"
            key={id}
            defaultValue={id}
            required
            type="number"
            min={1}
          />
        </label>
        <label>
          Действие
          <Select aria-label="Действие с аккаунтом" name="action">
            <option value="suspend_account">Отстранить на срок</option>
            <option value="block_account">Заблокировать навсегда</option>
            <option value="unblock_account">Снять блокировку аккаунта</option>
            <option value="chat_ban">Ограничить обсуждения</option>
            <option value="lift_chat">Снять ограничение обсуждений</option>
          </Select>
        </label>
        <label>
          Срок, часов
          <input
            name="hours"
            type="number"
            min={1}
            max={8760}
            defaultValue={24}
            required
          />
        </label>
        <label>
          Причина
          <input name="reason" required minLength={3} maxLength={512} />
        </label>
        <label className="checkbox-label">
          <input name="permanent" type="checkbox" /> Бессрочно ограничить
          обсуждения
        </label>
        <Button variant="primary">
          <Shield size={15} /> Применить ограничение
        </Button>
      </form>
      {mutation.isSuccess && (
        <p role="status" className="feedback">
          Ограничение обновлено
        </p>
      )}
      <Dialog
        open={Boolean(pending)}
        onClose={() => setPending(null)}
        title="Подтвердить изменение доступа"
      >
        <p>
          Пользователь #{pending?.id}. Причина: {pending?.reason}
        </p>
        <p className="muted">Действие попадёт в журнал модерации.</p>
        {mutation.error && <Status error={mutation.error} />}
        <div className="dialog-actions">
          <Button onClick={() => setPending(null)}>Отмена</Button>
          <Button
            variant="primary"
            busy={mutation.isPending}
            onClick={() => mutation.mutate(pending)}
          >
            Подтвердить
          </Button>
        </div>
      </Dialog>
    </section>
  );
}
function IpBans() {
  const query = usePages("/moderation/ip-bans", "bans", {}, { pageSize: 30 });
  const [deleting, setDeleting] = useState(null);
  const mutation = useMutation({
    mutationFn: ({ method, id, body }) =>
      request(`/moderation/ip-bans${id ? "/" + id : ""}`, { method, body }),
    onSuccess: () => {
      setDeleting(null);
      queryClient.invalidateQueries({
        queryKey: ["pages", "/moderation/ip-bans"],
      });
    },
  });
  return (
    <section>
      <h2>Блокировки IP</h2>
      <p className="muted">
        Блокировка действует на точный IPv4 или IPv6. Общий адрес может
        принадлежать нескольким людям; указывайте причину и разумный срок.
      </p>
      <form
        className="surface form-grid"
        onSubmit={(event) => {
          event.preventDefault();
          const form = Object.fromEntries(new FormData(event.currentTarget));
          mutation.mutate({
            method: "POST",
            body: { ...form, hours: Number(form.hours) },
          });
        }}
      >
        <label>
          IP-адрес
          <input
            name="ip"
            required
            maxLength={45}
            placeholder="IPv4 или IPv6"
          />
        </label>
        <label>
          Срок, часов (0 — навсегда)
          <input
            name="hours"
            type="number"
            min={0}
            max={8760}
            defaultValue={24}
            required
          />
        </label>
        <label>
          Причина
          <input name="reason" minLength={3} maxLength={512} required />
        </label>
        <Button variant="primary" busy={mutation.isPending}>
          Заблокировать IP
        </Button>
      </form>
      {mutation.error && !deleting && <Status error={mutation.error} />}
      {mutation.isSuccess && (
        <p role="status" className="feedback">
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
                <th>Адрес</th>
                <th>Причина</th>
                <th>Окончание</th>
                <th>Действие</th>
              </tr>
            </thead>
            <tbody>
              {pageItems(query, "bans").map((row) => (
                <tr key={row.id}>
                  <td>
                    <code>{row.ip || row.ip_address}</code>
                  </td>
                  <td>{row.reason}</td>
                  <td>{date(row.expires_at, true) || "Без срока"}</td>
                  <td>
                    <Button onClick={() => setDeleting(row)}>
                      <Trash2 size={14} /> Снять
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
        title="Снять блокировку IP"
      >
        <p>{deleting?.ip || deleting?.ip_address}</p>
        {mutation.error && <Status error={mutation.error} />}
        <div className="dialog-actions">
          <Button onClick={() => setDeleting(null)}>Отмена</Button>
          <Button
            variant="primary"
            busy={mutation.isPending}
            onClick={() =>
              mutation.mutate({ method: "DELETE", id: deleting.id })
            }
          >
            Снять блокировку
          </Button>
        </div>
      </Dialog>
    </section>
  );
}
function Audit() {
  const query = usePages("/moderation/audit", "events", {}, { pageSize: 30 });
  return (
    <section>
      <h2>Журнал действий</h2>
      <Status
        loading={query.isPending}
        error={!query.data && query.error}
        onRetry={query.refetch}
      >
        <div className="table-scroll">
          <table>
            <thead>
              <tr>
                <th>Дата</th>
                <th>Кто</th>
                <th>Действие</th>
                <th>Объект</th>
                <th>Причина</th>
              </tr>
            </thead>
            <tbody>
              {pageItems(query, "events").map((row) => (
                <tr key={row.id}>
                  <td>{date(row.created_at, true)}</td>
                  <td>
                    {row.actor_username ||
                      `#${row.actor_user_id || row.actor_id}`}
                  </td>
                  <td>{row.action}</td>
                  <td>
                    {row.target_user_id
                      ? `Пользователь #${row.target_user_id}`
                      : row.target_ip || row.target_id || "—"}
                  </td>
                  <td>{row.reason}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
        <LoadMore query={query} />
      </Status>
    </section>
  );
}
function Reports() {
  const query = usePages(
    "/staff/content-reports",
    "reports",
    {},
    { pageSize: 30 },
  );
  return (
    <section>
      <h2>Управление клипами</h2>
      <p className="muted">
        Скрытие убирает клип из публичного доступа. Синхронизация Twitch не
        отменяет решение модератора.
      </p>
      <ContentActionForm />
      <h2>Жалобы на контент</h2>
      <Status
        loading={query.isPending}
        error={!query.data && query.error}
        onRetry={query.refetch}
      >
        {pageItems(query, "reports").length === 0 && (
          <p className="empty-state">Жалоб пока нет.</p>
        )}
        {pageItems(query, "reports").map((report) => (
          <article className="surface report-card" key={report.id}>
            <div className="section-label">
              <Link className="text-link" to={`/clip/${report.clip_id}`}>
                Клип #{report.clip_id}
              </Link>
              <span className="subtle-badge">
                {{
                  pending: "Ожидает решения",
                  open: "Ожидает решения",
                  reviewed: "Обработана",
                  dismissed: "Отклонена",
                }[report.status] ||
                  report.status ||
                  "Ожидает решения"}
              </span>
            </div>
            <p className="post-body">{report.reason}</p>
            <small>{date(report.created_at)}</small>
            <ContentActionForm clipId={report.clip_id} reportId={report.id} />
          </article>
        ))}
        <LoadMore query={query} />
      </Status>
    </section>
  );
}
function ContentActionForm({ clipId, reportId }) {
  const mutation = useMutation({
    mutationFn: (body) => {
      if (["dismissed", "actioned"].includes(body.action))
        return request(`/moderation/reports/${reportId}/resolve`, {
          method: "POST",
          body: { resolution: body.action, reason: body.reason },
        });
      if (["hide", "restore"].includes(body.action))
        return request(`/moderation/clips/${body.id}/visibility`, {
          method: "POST",
          body: { hidden: body.action === "hide", reason: body.reason },
        });
      return request(`/moderation/clips/${body.id}/classification`, {
        method: "POST",
        body: { is_mature: body.action === "mature", reason: body.reason },
      });
    },
    onSuccess: () => {
      queryClient.invalidateQueries({
        queryKey: ["pages", "/staff/content-reports"],
      });
      queryClient.invalidateQueries({ queryKey: ["pages", "/clips"] });
      queryClient.invalidateQueries({ queryKey: ["recommendations"] });
    },
  });
  return (
    <form
      className="surface form-stack content-action"
      onSubmit={(event) => {
        event.preventDefault();
        const form = Object.fromEntries(new FormData(event.currentTarget));
        mutation.mutate({ ...form, id: Number(clipId || form.id) });
      }}
    >
      {!clipId && (
        <label>
          ID клипа
          <input name="id" type="number" min={1} required />
        </label>
      )}
      <label>
        Действие
        <Select aria-label="Действие с клипом" name="action">
          <option value="hide">Скрыть клип</option>
          <option value="restore">Вернуть клип</option>
          <option value="mature">Классифицировать: с ограничением</option>
          <option value="safe">
            Классифицировать: проверен, без ограничения
          </option>
          {reportId && (
            <>
              <option value="dismissed">Отклонить жалобу</option>
              <option value="actioned">Завершить обработку жалобы</option>
            </>
          )}
        </Select>
      </label>
      <label>
        Обоснование
        <input name="reason" minLength={3} maxLength={256} required />
      </label>
      <Button busy={mutation.isPending}>Применить решение</Button>
      {mutation.error && <Status error={mutation.error} />}
      {mutation.isSuccess && (
        <p className="feedback" role="status">
          Решение сохранено
        </p>
      )}
    </form>
  );
}
