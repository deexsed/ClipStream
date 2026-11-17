import { ConfirmationActions } from "../../shared/ui/Confirmation";
import DataTable from "../../shared/ui/DataTable";
import { SectionHeading } from "../../shared/ui/Layout";
import { Feedback, EmptyState } from "../../shared/ui/Feedback";
import { Input, TextArea, Field, Checkbox } from "../../shared/ui/FormControls";
import { useState, useRef, useLayoutEffect, useEffect } from "react";
import { useSearchParams } from "react-router-dom";
import { useMutation } from "@tanstack/react-query";
import { Shield, ExternalLink } from "lucide-react";
import Select from "../../shared/ui/Select";
import SearchField from "../../shared/ui/SearchField";
import {
  Identity,
  Button,
  Dialog,
  LoadMore,
  Status,
  ButtonLink,
} from "../../shared/ui/Primitives";
import {
  useApi,
  usePages,
  pageItems,
  queryClient,
} from "../../shared/api/queries";
import { request } from "../../shared/api/http";
import { date } from "../../shared/format";
import { useSession } from "../auth/Session";

export const roleNames = {
  user: "Пользователь",
  vip: "VIP",
  moderator: "Модератор",
  admin: "Администратор",
};
export const actionNames = {
  suspend_account: "Отстранить на срок",
  block_account: "Заблокировать навсегда",
  unblock_account: "Восстановить доступ",
  chat_ban: "Ограничить обсуждения",
  lift_chat: "Разрешить обсуждения",
  set_role: "Изменить роль",
};
const future = (value) =>
  value &&
  new Date(
    /^\d{4}-\d{2}-\d{2} /.test(value) ? value.replace(" ", "T") + "Z" : value,
  ).getTime() > Date.now();
export function accountStatus(user) {
  return user.account_blocked
    ? "Заблокирован"
    : future(user.account_suspended_until)
      ? "Отстранён"
      : "Активен";
}
const chatRestricted = (user) =>
  user.chat_banned_permanently || future(user.chat_banned_until);

export default function UsersManager() {
  const { user: actor } = useSession();
  const [params, setParams] = useSearchParams();
  const [search, setSearch] = useState(params.get("q") || "");
  const status = params.get("status") || "";
  const q = params.get("q") || "";
  useEffect(() => setSearch(q), [q]);
  const selectedId = params.get("user");
  const busy = useRef(false);
  const query = usePages(
    "/moderation/users",
    "users",
    { q, status },
    { pageSize: 30, refetchInterval: 30000 },
  );
  const rows = pageItems(query, "users");
  const update = (changes) => {
    const next = new URLSearchParams(params);
    for (const [key, value] of Object.entries(changes))
      value ? next.set(key, value) : next.delete(key);
    setParams(next, { replace: true });
  };
  return (
    <section className="console-users">
      <SectionHeading>
        <div>
          <h2>Пользователи и доступ</h2>
          <p className="muted">
            Профиль, роль и ограничения — в одной карточке.
          </p>
        </div>
        <span className="subtle-badge">
          {query.data?.pages[0]?.total ?? "—"} найдено
        </span>
      </SectionHeading>
      <form
        className="console-toolbar"
        onSubmit={(event) => {
          event.preventDefault();
          update({ q: search.trim() });
        }}
      >
        <SearchField
          className="console-search"
          aria-label="Найти пользователя"
          placeholder={
            actor?.role === 2
              ? "Имя пользователя или почта…"
              : "Имя пользователя…"
          }
          maxLength={64}
          value={search}
          onValueChange={setSearch}
          onClear={() => update({ q: null })}
        />
        <Button type="submit">Найти</Button>
        <Select
          aria-label="Статус аккаунта"
          value={status || "all"}
          onChange={(event) =>
            update({
              status: event.target.value === "all" ? "" : event.target.value,
            })
          }
        >
          <option value="all">Все пользователи</option>
          <option value="active">Активные</option>
          <option value="blocked">Заблокированные</option>
          <option value="suspended">Отстранённые</option>
          <option value="chat">Ограничены обсуждения</option>
        </Select>
      </form>
      <Status
        loading={query.isPending}
        error={query.error}
        onRetry={query.refetch}
      >
        <DataTable label="Пользователи и доступ">
          <thead>
            <tr>
              <th>Пользователь</th>
              <th>Роль</th>
              <th>Доступ</th>
              <th>Регистрация</th>
              <th>
                <span className="sr-only">Действия</span>
              </th>
            </tr>
          </thead>
          <tbody>
            {rows.map((user) => (
              <tr key={user.id}>
                <td>
                  <Identity user={user}>
                    <div>
                      <strong>{user.username}</strong>
                      {user.email && (
                        <small className="block muted">{user.email}</small>
                      )}
                    </div>
                  </Identity>
                </td>
                <td data-label="Роль">{roleNames[user.role] || user.role}</td>
                <td data-label="Доступ">
                  <span
                    className={`console-state ${accountStatus(user) === "Активен" ? "" : "console-state-warning"}`}
                  >
                    {accountStatus(user)}
                  </span>
                  {chatRestricted(user) && (
                    <small className="block muted">Обсуждения ограничены</small>
                  )}
                </td>
                <td data-label="Регистрация">{date(user.created_at) || "—"}</td>
                <td>
                  <Button
                    size="compact"
                    onClick={() => update({ user: String(user.id) })}
                    aria-label={`Управлять доступом ${user.username}`}
                  >
                    <Shield size={15} /> Управлять
                  </Button>
                </td>
              </tr>
            ))}
          </tbody>
        </DataTable>

        {rows.length === 0 && (
          <EmptyState>
            <h3>Пользователи не найдены</h3>
            <p>Попробуйте другое имя или измените фильтр доступа.</p>
          </EmptyState>
        )}
        <LoadMore query={query} />
      </Status>
      <Dialog
        open={Boolean(selectedId)}
        onClose={() => {
          if (!busy.current) update({ user: null });
        }}
        title="Карточка пользователя"
        className="console-user-dialog"
      >
        {selectedId && (
          <UserAccess
            key={selectedId}
            id={selectedId}
            actor={actor}
            busy={busy}
          />
        )}
      </Dialog>
    </section>
  );
}

function UserAccess({ id, actor, busy }) {
  const state = useApi(`/moderation/users/${encodeURIComponent(id)}`, {
    staleTime: 0,
    refetchInterval: 30000,
  });
  const [selectedAction, setAction] = useState(null);
  const [reason, setReason] = useState("");
  const [hours, setHours] = useState("24");
  const [permanent, setPermanent] = useState(false);
  const [pending, setPending] = useState(null);
  const confirmHeading = useRef(null);
  const continueButton = useRef(null);
  const previousPending = useRef(null);
  useLayoutEffect(() => {
    if (pending) confirmHeading.current?.focus();
    else if (previousPending.current)
      continueButton.current?.querySelector('button[type="submit"]')?.focus();
    previousPending.current = pending;
  }, [pending]);
  const mutation = useMutation({
    onMutate: () => {
      busy.current = true;
    },
    onSettled: () => {
      busy.current = false;
    },
    mutationFn: (body) =>
      body.action === "set_role"
        ? request(`/admin/users/${id}/role`, {
            method: "POST",
            body: { role: body.role },
          })
        : request(`/moderation/users/${id}/restriction`, {
            method: "POST",
            body,
          }),
    onSuccess: async (_, body) => {
      await Promise.all([
        queryClient.invalidateQueries({
          queryKey: ["api", `/moderation/users/${id}`],
        }),
        queryClient.invalidateQueries({
          queryKey: ["pages", "/moderation/users"],
        }),
        queryClient.invalidateQueries({ queryKey: ["pages", "/admin/users"] }),
        queryClient.invalidateQueries({
          queryKey: ["api", "/admin/dashboard"],
        }),
        queryClient.invalidateQueries({
          queryKey: ["pages", "/moderation/audit"],
        }),
      ]);
      setPending(null);
      setAction(null);
      if (body.action !== "set_role") setReason("");
      if (body.action === "set_role" && Number(id) === actor?.id)
        queryClient.invalidateQueries({ queryKey: ["session"] });
    },
  });
  const user = state.data;
  const cannotModerate =
    user && actor?.role !== 2 && ["admin", "moderator"].includes(user.role);
  const cannotRestrict = user && actor?.id === user.id;
  const action =
    selectedAction ??
    (user && (user.account_blocked || future(user.account_suspended_until))
      ? "unblock_account"
      : "suspend_account");
  const duration =
    action === "suspend_account" || (action === "chat_ban" && !permanent);
  return (
    <Status
      loading={state.isPending}
      error={state.error}
      onRetry={state.refetch}
    >
      {user && (
        <>
          <div className="console-user-summary">
            <div>
              <h3>{user.username}</h3>
              <span className="muted">
                {roleNames[user.role]} · #{user.id}
              </span>
            </div>
            <ButtonLink to={`/u/${encodeURIComponent(user.username)}`}>
              Профиль <ExternalLink size={14} />
            </ButtonLink>
          </div>
          <dl className="console-access-state">
            <div>
              <dt>Аккаунт</dt>
              <dd>
                {accountStatus(user)}
                {future(user.account_suspended_until) &&
                  !user.account_blocked &&
                  ` до ${date(user.account_suspended_until, true)}`}
              </dd>
              {user.account_ban_reason && (
                <dd className="muted">{user.account_ban_reason}</dd>
              )}
            </div>
            <div>
              <dt>Обсуждения</dt>
              <dd>
                {chatRestricted(user)
                  ? user.chat_banned_permanently
                    ? "Ограничены бессрочно"
                    : `Ограничены до ${date(user.chat_banned_until, true)}`
                  : "Разрешены"}
              </dd>
              {chatRestricted(user) && user.chat_ban_reason && (
                <dd className="muted">{user.chat_ban_reason}</dd>
              )}
            </div>
          </dl>
          {pending ? (
            <div
              className="console-confirm"
              role="region"
              aria-label="Подтвердить изменение доступа"
            >
              <h3 tabIndex={-1} ref={confirmHeading}>
                Подтвердить изменение доступа
              </h3>
              <p>
                <strong>{user.username}</strong> · {actionNames[pending.action]}
              </p>
              {pending.action === "set_role" ? (
                <p>
                  {roleNames[user.role]} → {roleNames[pending.role]}
                </p>
              ) : (
                <>
                  <p>Причина: {pending.reason}</p>
                  {["chat_ban", "suspend_account"].includes(pending.action) && (
                    <p>
                      Срок:{" "}
                      {pending.permanent ? "бессрочно" : `${pending.hours} ч.`}
                    </p>
                  )}
                </>
              )}
              {["suspend_account", "block_account"].includes(
                pending.action,
              ) && (
                <p className="muted">
                  Вход будет закрыт, действующие сессии отозваны.
                </p>
              )}
              <p className="muted">Действие попадёт в журнал модерации.</p>
              {mutation.error && <Status error={mutation.error} />}
              <ConfirmationActions
                pending={mutation.isPending}
                onCancel={() => {
                  setPending(null);
                  mutation.reset();
                }}
                onConfirm={() => mutation.mutate(pending)}
                cancelLabel="Назад"
              />
            </div>
          ) : (
            <>
              {cannotModerate ? (
                <p className="muted" role="status">
                  Недостаточно прав для изменения доступа сотрудника с равной
                  или более высокой ролью.
                </p>
              ) : (
                <form
                  ref={continueButton}
                  className="form-stack"
                  onSubmit={(event) => {
                    event.preventDefault();
                    const form = Object.fromEntries(
                      new FormData(event.currentTarget),
                    );
                    if (reason.trim().length < 3) {
                      const field = event.currentTarget.elements.reason;
                      field.setCustomValidity(
                        "Укажите причину: не менее трёх символов без пробелов по краям.",
                      );
                      field.reportValidity();
                      return;
                    }
                    mutation.reset();
                    setPending({
                      action,
                      reason: form.reason.trim(),
                      ...(duration ? { hours: Number(form.hours) } : {}),
                      ...(action === "chat_ban" ? { permanent } : {}),
                    });
                  }}
                >
                  <h3>Изменить доступ</h3>
                  {cannotRestrict && (
                    <p className="muted">Нельзя ограничить свой аккаунт.</p>
                  )}
                  <Field>
                    Действие
                    <Select
                      aria-label="Действие с аккаунтом"
                      value={action}
                      onChange={(event) => setAction(event.target.value)}
                    >
                      {Object.entries(actionNames)
                        .filter(([key]) => key !== "set_role")
                        .map(([key, label]) => (
                          <option
                            key={key}
                            value={key}
                            disabled={
                              cannotRestrict &&
                              [
                                "suspend_account",
                                "block_account",
                                "chat_ban",
                              ].includes(key)
                            }
                          >
                            {label}
                          </option>
                        ))}
                    </Select>
                  </Field>
                  {action === "chat_ban" && (
                    <Checkbox
                      checked={permanent}
                      onChange={(event) => setPermanent(event.target.checked)}
                    >
                      Бессрочно
                    </Checkbox>
                  )}
                  {duration && (
                    <Field>
                      Срок, часов
                      <Input
                        name="hours"
                        type="number"
                        min={1}
                        max={8760}
                        value={hours}
                        onChange={(event) => setHours(event.target.value)}
                        required
                      />
                    </Field>
                  )}
                  <Field>
                    Причина
                    <TextArea
                      name="reason"
                      minLength={3}
                      maxLength={128}
                      value={reason}
                      onChange={(event) => {
                        event.target.setCustomValidity("");
                        setReason(event.target.value);
                      }}
                      required
                    />
                  </Field>
                  <Button
                    type="submit"
                    variant="primary"
                    disabled={
                      cannotRestrict &&
                      ["suspend_account", "block_account", "chat_ban"].includes(
                        action,
                      )
                    }
                  >
                    Продолжить
                  </Button>
                </form>
              )}
              {actor?.role === 2 && (
                <form
                  className="form-stack console-role-form"
                  onSubmit={(event) => {
                    event.preventDefault();
                    const role = new FormData(event.currentTarget).get("role");
                    if (role !== user.role) {
                      mutation.reset();
                      setPending({ action: "set_role", role });
                    }
                  }}
                >
                  <h3>Права на платформе</h3>
                  <Field>
                    Роль
                    <Select
                      key={user.role}
                      aria-label={`Роль ${user.username}`}
                      name="role"
                      defaultValue={user.role}
                    >
                      {Object.entries(roleNames).map(([key, label]) => (
                        <option key={key} value={key}>
                          {label}
                        </option>
                      ))}
                    </Select>
                  </Field>
                  <Button>Изменить роль</Button>
                </form>
              )}
              {mutation.isSuccess && (
                <Feedback role="status">Изменения сохранены</Feedback>
              )}
            </>
          )}
        </>
      )}
    </Status>
  );
}
