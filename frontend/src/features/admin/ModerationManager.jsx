import ConfirmDialog from "../../shared/ui/Confirmation";
import DataTable from "../../shared/ui/DataTable";
import { Surface, SectionHeading } from "../../shared/ui/Layout";
import { Feedback, EmptyState } from "../../shared/ui/Feedback";
import { Input, Field } from "../../shared/ui/FormControls";
import Select from "../../shared/ui/Select";
import { useState } from "react";
import { Link } from "react-router-dom";
import { useMutation } from "@tanstack/react-query";
import { Trash2 } from "lucide-react";
import { request } from "../../shared/api/http";
import { usePages, pageItems, queryClient } from "../../shared/api/queries";
import { Button, LoadMore, Status } from "../../shared/ui/Primitives";
import UsersManager, { actionNames } from "./UsersManager";
import { date } from "../../shared/format";
export default function ModerationManager({ section = "accounts" }) {
  return section === "ip" ? (
    <IpBans />
  ) : section === "audit" ? (
    <Audit />
  ) : section === "reports" ? (
    <Reports />
  ) : (
    <UsersManager />
  );
}
function IpBans() {
  const query = usePages("/moderation/ip-bans", "bans", {}, { pageSize: 30 });
  const [deleting, setDeleting] = useState(null);
  const [creating, setCreating] = useState(null);
  const mutation = useMutation({
    mutationFn: ({ method, id, body }) =>
      request(`/moderation/ip-bans${id ? "/" + id : ""}`, { method, body }),
    onSuccess: () => {
      setDeleting(null);
      setCreating(null);
      queryClient.invalidateQueries({
        queryKey: ["pages", "/moderation/audit"],
      });
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
      <Surface
        as="form"
        className="form-grid"
        onSubmit={(event) => {
          event.preventDefault();
          const form = Object.fromEntries(new FormData(event.currentTarget));
          mutation.reset();
          setCreating({ ...form, hours: Number(form.hours) });
        }}
      >
        <Field>
          IP-адрес
          <Input
            name="ip"
            required
            maxLength={45}
            placeholder="IPv4 или IPv6"
          />
        </Field>
        <Field>
          Срок, часов (0 — навсегда)
          <Input
            name="hours"
            type="number"
            min={0}
            max={8760}
            defaultValue={24}
            required
          />
        </Field>
        <Field>
          Причина
          <Input name="reason" minLength={3} maxLength={512} required />
        </Field>
        <Button variant="primary" busy={mutation.isPending}>
          Заблокировать IP
        </Button>
      </Surface>
      <ConfirmDialog
        open={Boolean(creating)}
        onClose={() => {
          if (!mutation.isPending) setCreating(null);
        }}
        title="Заблокировать IP"
        pending={mutation.isPending}
        onConfirm={() => mutation.mutate({ method: "POST", body: creating })}
        confirmLabel={<>Подтвердить</>}
        onCancel={() => setCreating(null)}
        cancelLabel={<>Отмена</>}
        error={mutation.error}
      >
        <p>
          <strong>{creating?.ip}</strong> ·{" "}
          {creating?.hours ? `${creating.hours} ч.` : "Бессрочно"}
        </p>
        <p>{creating?.reason}</p>
        <p className="muted">
          Доступ с этого адреса будет закрыт для всех пользователей общей сети.
        </p>
      </ConfirmDialog>
      {mutation.isSuccess && (
        <Feedback role="status">Изменения сохранены</Feedback>
      )}
      <Status
        loading={query.isPending}
        error={!query.data && query.error}
        onRetry={query.refetch}
      >
        <DataTable label="Блокировки IP">
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
                  <Button
                    size="compact"
                    onClick={() => {
                      mutation.reset();
                      setDeleting(row);
                    }}
                  >
                    <Trash2 size={14} /> Снять
                  </Button>
                </td>
              </tr>
            ))}
          </tbody>
        </DataTable>

        {pageItems(query, "bans").length === 0 && (
          <EmptyState as="p">Блокировок IP нет.</EmptyState>
        )}
        <LoadMore query={query} />
      </Status>
      <ConfirmDialog
        open={Boolean(deleting)}
        onClose={() => setDeleting(null)}
        title="Снять блокировку IP"
        pending={mutation.isPending}
        onConfirm={() => mutation.mutate({ method: "DELETE", id: deleting.id })}
        confirmLabel={<>Снять блокировку</>}
        onCancel={() => setDeleting(null)}
        cancelLabel={<>Отмена</>}
        error={mutation.error}
      >
        <p>{deleting?.ip || deleting?.ip_address}</p>
      </ConfirmDialog>
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
        <DataTable label="Журнал действий">
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
                <td>
                  {actionNames[row.action] ||
                    {
                      ban_ip: "Блокировка IP",
                      unban_ip: "Снятие блокировки IP",
                      resolve_report: "Решение по жалобе",
                      hide_clip: "Скрытие клипа",
                      restore_clip: "Восстановление клипа",
                      classify_clip: "Классификация клипа",
                      delete_comment: "Удаление комментария",
                      bootstrap_admin: "Назначение администратора",
                    }[row.action] ||
                    row.action}
                </td>
                <td>
                  {row.target_type === "user" ? (
                    <Link
                      className="text-link"
                      to={`?tab=users&user=${encodeURIComponent(row.target_id)}`}
                    >
                      {row.target_username || `Пользователь #${row.target_id}`}
                    </Link>
                  ) : row.target_type === "clip" ? (
                    <Link className="text-link" to={`/clip/${row.target_id}`}>
                      Клип #{row.target_id}
                    </Link>
                  ) : row.target_type === "comment" ? (
                    row.details?.clip_id ? (
                      <Link
                        className="text-link"
                        to={`/clip/${row.details.clip_id}`}
                      >
                        Комментарий #{row.target_id}
                      </Link>
                    ) : (
                      `Комментарий #${row.target_id}`
                    )
                  ) : (
                    `${row.target_type === "ip" ? "IP" : "Жалоба"} ${row.target_id}`
                  )}
                </td>
                <td>{row.reason}</td>
              </tr>
            ))}
          </tbody>
        </DataTable>

        {pageItems(query, "events").length === 0 && (
          <EmptyState as="p">Журнал пока пуст.</EmptyState>
        )}
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
      <h2>Жалобы на контент и пользователей</h2>
      <p className="muted">
        Откройте объект жалобы, проверьте контекст и примите решение. Изменение
        контента и закрытие жалобы — отдельные действия.
      </p>
      <Status
        loading={query.isPending}
        error={!query.data && query.error}
        onRetry={query.refetch}
      >
        {pageItems(query, "reports").length === 0 && (
          <EmptyState as="p">Жалоб пока нет.</EmptyState>
        )}
        {pageItems(query, "reports").map((report) => (
          <Surface as="article" className="report-card" key={report.id}>
            <SectionHeading>
              {report.target_type === "clip" ? (
                <Link className="text-link" to={`/clip/${report.target_id}`}>
                  {report.clip_title || `Клип #${report.target_id}`}
                </Link>
              ) : report.target_type === "user" ? (
                <Link
                  className="text-link"
                  to={`?tab=users&user=${report.target_id}`}
                >
                  Пользователь #{report.target_id}
                </Link>
              ) : (
                <span>Комментарий #{report.target_id}</span>
              )}
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
            </SectionHeading>
            <p className="post-body">{report.reason}</p>
            <small>{date(report.created_at)}</small>
            <p className="muted">
              Отправитель:{" "}
              {report.reporter_username || `#${report.reporter_id}`}
            </p>
            {report.comment_content && (
              <blockquote className="post-body">
                {report.comment_content}
              </blockquote>
            )}
            {report.target_type === "comment" && (
              <div className="dialog-actions">
                {report.clip_id && (
                  <Link className="text-link" to={`/clip/${report.clip_id}`}>
                    Открыть обсуждение
                  </Link>
                )}
                {report.subject_user_id && (
                  <Link
                    className="text-link"
                    to={`?tab=users&user=${report.subject_user_id}`}
                  >
                    Управлять автором
                  </Link>
                )}
              </div>
            )}
            <ContentActionForm
              clipId={report.target_type === "clip" ? report.target_id : null}
              reportId={report.id}
              targetType={report.target_type}
              targetId={report.target_id}
              contextClipId={report.clip_id}
            />
          </Surface>
        ))}
        <LoadMore query={query} />
      </Status>
      <Surface as="details" className="console-content-tools">
        <summary>Управление клипом по ID</summary>
        <p className="muted">
          Скрытие убирает клип из публичного доступа. Синхронизация Twitch не
          отменяет решение модератора.
        </p>
        <ContentActionForm />
      </Surface>
    </section>
  );
}
function ContentActionForm({
  clipId,
  reportId,
  targetType,
  targetId,
  contextClipId,
}) {
  const [pending, setPending] = useState(null);
  const mutation = useMutation({
    mutationFn: (body) => {
      if (body.action === "delete_comment")
        return request(`/comments/${targetId}`, {
          method: "DELETE",
          body: { reason: body.reason },
        });
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
      setPending(null);
      queryClient.invalidateQueries({
        queryKey: ["pages", "/moderation/audit"],
      });
      queryClient.invalidateQueries({
        queryKey: ["pages", "/staff/content-reports"],
      });
      queryClient.invalidateQueries({ queryKey: ["pages", "/clips"] });
      queryClient.invalidateQueries({ queryKey: ["recommendations"] });
      queryClient.invalidateQueries({ queryKey: ["api", "/admin/dashboard"] });
      const affectedClip = clipId || contextClipId || pending?.id;
      if (affectedClip) {
        queryClient.invalidateQueries({
          queryKey: ["api", `/clips/${affectedClip}`],
        });
        queryClient.invalidateQueries({
          queryKey: ["pages", `/clips/${affectedClip}/comments`],
        });
      }
    },
  });
  return (
    <Surface
      as="form"
      className="form-stack content-action"
      onSubmit={(event) => {
        event.preventDefault();
        const form = Object.fromEntries(new FormData(event.currentTarget));
        mutation.reset();
        setPending({ ...form, id: Number(clipId || form.id) });
      }}
    >
      {!clipId && !reportId && (
        <Field>
          ID клипа
          <Input name="id" type="number" min={1} required />
        </Field>
      )}
      <Field>
        Действие
        <Select aria-label="Действие с клипом" name="action">
          {(!reportId || targetType === "clip") && (
            <>
              <option value="hide">Скрыть клип</option>
              <option value="restore">Вернуть клип</option>
              <option value="mature">Классифицировать: с ограничением</option>
              <option value="safe">
                Классифицировать: проверен, без ограничения
              </option>
            </>
          )}
          {targetType === "comment" && (
            <option value="delete_comment">Удалить комментарий</option>
          )}
          {reportId && (
            <>
              <option value="dismissed">Отклонить жалобу</option>
              <option value="actioned">Завершить обработку жалобы</option>
            </>
          )}
        </Select>
      </Field>
      <Field>
        Обоснование
        <Input name="reason" minLength={3} maxLength={256} required />
      </Field>
      <Button busy={mutation.isPending}>Применить решение</Button>
      <ConfirmDialog
        open={Boolean(pending)}
        onClose={() => {
          if (!mutation.isPending) setPending(null);
        }}
        title="Подтвердить решение"
        pending={mutation.isPending}
        onConfirm={() => mutation.mutate(pending)}
        confirmLabel={<>Подтвердить</>}
        onCancel={() => setPending(null)}
        cancelLabel={<>Отмена</>}
        error={mutation.error}
      >
        <p>
          Объект: {reportId ? `жалоба #${reportId}` : `клип #${pending?.id}`}.
          Действие:{" "}
          {
            {
              hide: "Скрыть клип",
              restore: "Вернуть клип",
              mature: "Установить возрастное ограничение",
              safe: "Снять возрастное ограничение",
              dismissed: "Отклонить жалобу",
              actioned: "Завершить обработку жалобы",
              delete_comment: "Удалить комментарий",
            }[pending?.action]
          }
        </p>
        <p>{pending?.reason}</p>
      </ConfirmDialog>
      {mutation.isSuccess && (
        <Feedback role="status">Решение сохранено</Feedback>
      )}
    </Surface>
  );
}
