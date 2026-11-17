import React from "react";
import { it, expect, vi } from "vitest";
import {
  render,
  screen,
  fireEvent,
  waitFor,
  within,
} from "@testing-library/react";
import { MemoryRouter, Routes, Route, Outlet } from "react-router-dom";
import { QueryClientProvider } from "@tanstack/react-query";
import { queryClient } from "../src/shared/api/queries";
import { SessionProvider } from "../src/features/auth/Session";
import ConsolePage from "../src/features/admin/ConsolePage";
const json = (data, status = 200) =>
  new Response(JSON.stringify(data), { status });
function setup(tab, fetcher, { role = 2, admin = true } = {}) {
  localStorage.setItem("token", "fixture");
  vi.spyOn(globalThis, "fetch").mockImplementation(async (url, options) =>
    url.endsWith("/auth/me")
      ? json({ user: { id: 1, username: "Admin", role } })
      : fetcher(url, options),
  );
  render(
    <MemoryRouter initialEntries={["/admin?tab=" + tab]}>
      <QueryClientProvider client={queryClient}>
        <SessionProvider>
          <Routes>
            <Route element={<Outlet context={{ openLogin: () => {} }} />}>
              <Route path="/admin" element={<ConsolePage admin={admin} />} />
            </Route>
          </Routes>
        </SessionProvider>
      </QueryClientProvider>
    </MemoryRouter>,
  );
}
it("edits a campaign without resubmitting server-owned fields", async () => {
  const campaign = {
    id: 9,
    name: "Existing",
    type: "banner",
    title: "Previous title",
    body: "",
    advertiser: "Fixture advertiser",
    image_url: "https://media.example.test/a.jpg",
    click_url: "https://example.org/",
    enabled: false,
    priority: 100,
    daily_cap: 3,
    cooldown_seconds: 0,
    skip_after_seconds: 0,
    metrics: { served: 99, impressions: 12, clicks: 2 },
  };
  let submitted;
  setup("ads", async (url, options) => {
    if (options.method === "PUT") {
      submitted = JSON.parse(options.body);
      return json({ campaign: { ...campaign, ...submitted } });
    }
    return json(
      url.includes("/metrics")
        ? { served: 99, impressions: 12, clicks: 2 }
        : { campaigns: [campaign], has_more: false },
    );
  });
  await screen.findByText("Existing");
  fireEvent.click(screen.getByRole("button", { name: "Изменить" }));
  const dialog = screen.getByRole("dialog", { name: "Редактировать кампанию" });
  fireEvent.change(within(dialog).getByLabelText("Заголовок"), {
    target: { value: "Updated title" },
  });
  fireEvent.click(
    within(dialog).getByRole("button", { name: "Сохранить кампанию" }),
  );
  await waitFor(() => expect(submitted?.title).toBe("Updated title"));
  expect(submitted.id).toBeUndefined();
  expect(submitted.metrics).toBeUndefined();
  expect(submitted.enabled).toBe(false);
});

const target = {
  id: 1002,
  username: "Fixture user",
  role: "user",
  account_blocked: false,
  chat_banned_permanently: false,
  created_at: "2026-11-01 12:00:00",
};
async function openUser() {
  fireEvent.click(
    await screen.findByRole("button", {
      name: "Управлять доступом Fixture user",
    }),
  );
  const dialog = await screen.findByRole("dialog", {
    name: "Карточка пользователя",
  });
  await within(dialog).findByRole("heading", { name: "Fixture user" });
  return dialog;
}
async function choose(name, option) {
  fireEvent.keyDown(screen.getByRole("combobox", { name }), {
    key: "ArrowDown",
  });
  fireEvent.click(
    await screen.findByRole("option", { name: option, exact: true }),
  );
}
function userFetcher(post = () => json({ ok: true })) {
  return (url, options) =>
    options.method === "POST"
      ? post(url, options)
      : json(
          url.includes("/moderation/users?")
            ? { users: [target], total: 1, has_more: false }
            : target,
        );
}
it("selects a named account, confirms suspension, and retains a failed action", async () => {
  const post = vi.fn(() => json({ error: "Temporary service failure" }, 503));
  setup("moderation", userFetcher(post));
  const dialog = await openUser();
  expect(within(dialog).queryByLabelText("ID пользователя")).toBeNull();
  fireEvent.change(within(dialog).getByLabelText("Причина"), {
    target: { value: "Fixture moderation reason" },
  });
  fireEvent.click(within(dialog).getByRole("button", { name: "Продолжить" }));
  expect(post).not.toHaveBeenCalled();
  expect(
    within(dialog).getByRole("region", {
      name: "Подтвердить изменение доступа",
    }).textContent,
  ).toContain("Fixture user");
  fireEvent.click(within(dialog).getByRole("button", { name: "Подтвердить" }));
  await within(dialog).findByText("Temporary service failure");
  expect(post).toHaveBeenCalledTimes(1);
  expect(post.mock.calls[0][0]).toBe("/api/moderation/users/1002/restriction");
  expect(JSON.parse(post.mock.calls[0][1].body)).toEqual({
    action: "suspend_account",
    hours: 24,
    reason: "Fixture moderation reason",
  });
  expect(dialog.open).toBe(true);
  expect(screen.queryByText("Изменения сохранены")).toBeNull();
});
it("sends a permanent chat restriction without a hidden duration", async () => {
  const post = vi.fn(() => json({ ok: true }));
  setup("users", userFetcher(post));
  const dialog = await openUser();
  await choose("Действие с аккаунтом", "Ограничить обсуждения");
  fireEvent.click(within(dialog).getByLabelText("Бессрочно"));
  expect(within(dialog).queryByLabelText("Срок, часов")).toBeNull();
  fireEvent.change(within(dialog).getByLabelText("Причина"), {
    target: { value: "Repeated spam" },
  });
  fireEvent.click(within(dialog).getByRole("button", { name: "Продолжить" }));
  fireEvent.click(within(dialog).getByRole("button", { name: "Подтвердить" }));
  await waitFor(() => expect(post).toHaveBeenCalledTimes(1));
  expect(JSON.parse(post.mock.calls[0][1].body)).toEqual({
    action: "chat_ban",
    permanent: true,
    reason: "Repeated spam",
  });
  await within(dialog).findByText("Изменения сохранены");
});
it("searches on submission and sends the status filter to the server", async () => {
  const urls = [];
  setup("users", (url) => {
    urls.push(url);
    return json({ users: [], total: 0, has_more: false });
  });
  const search = await screen.findByRole("searchbox", {
    name: "Найти пользователя",
  });
  fireEvent.change(search, { target: { value: "  Алиса  " } });
  expect(urls.some((url) => url.includes(encodeURIComponent("Алиса")))).toBe(
    false,
  );
  fireEvent.click(screen.getByRole("button", { name: "Найти" }));
  await waitFor(() =>
    expect(
      urls.some((url) => url.includes("q=" + encodeURIComponent("Алиса"))),
    ).toBe(true),
  );
  await choose("Статус аккаунта", "Заблокированные");
  await waitFor(() =>
    expect(urls.some((url) => url.includes("status=blocked"))).toBe(true),
  );
});
it("binds a clip report to target_id and confirms visibility changes", async () => {
  const post = vi.fn(() => json({ ok: true }));
  setup("reports", (url, options) =>
    options.method === "POST"
      ? post(url, options)
      : json({
          reports: [
            {
              id: 17,
              target_type: "clip",
              target_id: 42,
              clip_title: "Reported clip",
              reason: "Spam",
              status: "open",
            },
          ],
        }),
  );
  const card = (
    await screen.findByRole("link", { name: "Reported clip" })
  ).closest("article");
  expect(within(card).queryByLabelText("ID клипа")).toBeNull();
  fireEvent.change(within(card).getByLabelText("Обоснование"), {
    target: { value: "Confirmed spam" },
  });
  fireEvent.click(
    within(card).getByRole("button", { name: "Применить решение" }),
  );
  expect(post).not.toHaveBeenCalled();
  fireEvent.click(
    within(
      screen.getByRole("dialog", { name: "Подтвердить решение" }),
    ).getByRole("button", { name: "Подтвердить" }),
  );
  await waitFor(() => expect(post).toHaveBeenCalledTimes(1));
  expect(post.mock.calls[0][0]).toBe("/api/moderation/clips/42/visibility");
});
it("opens user reports as accounts and never asks for a clip ID", async () => {
  setup("reports", () =>
    json({
      reports: [
        {
          id: 18,
          target_type: "user",
          target_id: 1002,
          reason: "Harassment",
          status: "open",
        },
      ],
    }),
  );
  const link = await screen.findByRole("link", { name: "Пользователь #1002" });
  expect(link.getAttribute("href")).toContain("tab=users&user=1002");
  expect(
    within(link.closest("article")).queryByLabelText("ID клипа"),
  ).toBeNull();
  expect(
    within(link.closest("article")).getByRole("combobox").textContent,
  ).toBe("Отклонить жалобу");
});
it("opens a named audit target directly in the access card", async () => {
  setup("audit", () =>
    json({
      events: [
        {
          id: 1,
          actor_username: "Admin",
          target_type: "user",
          target_id: "1002",
          target_username: "Fixture user",
          action: "suspend_account",
          reason: "Spam",
        },
      ],
    }),
  );
  const link = await screen.findByRole("link", { name: "Fixture user" });
  expect(link.getAttribute("href")).toContain("tab=users&user=1002");
  expect(screen.getByText("Отстранить на срок")).toBeTruthy();
});
it("requires confirmation before blocking an IP", async () => {
  const post = vi.fn(() => json({ ok: true }));
  setup("ip", (url, options) =>
    options.method === "POST"
      ? post(url, options)
      : json({ bans: [], has_more: false }),
  );
  await screen.findByRole("heading", { name: "Блокировки IP" });
  fireEvent.change(screen.getByLabelText("IP-адрес"), {
    target: { value: "192.0.2.10" },
  });
  fireEvent.change(screen.getByLabelText("Причина"), {
    target: { value: "Abuse from this network" },
  });
  fireEvent.click(screen.getByRole("button", { name: "Заблокировать IP" }));
  expect(post).not.toHaveBeenCalled();
  fireEvent.click(
    within(screen.getByRole("dialog", { name: "Заблокировать IP" })).getByRole(
      "button",
      { name: "Подтвердить" },
    ),
  );
  await waitFor(() => expect(post).toHaveBeenCalledTimes(1));
});

it("keeps account roles unavailable to moderators", async () => {
  setup("users", userFetcher(), { role: 1, admin: false });
  const dialog = await openUser();
  expect(
    within(dialog).queryByRole("combobox", { name: "Роль Fixture user" }),
  ).toBeNull();
  expect(
    screen.queryByRole("button", { name: "Реклама", exact: true }),
  ).toBeNull();
  expect(screen.getByRole("searchbox").getAttribute("placeholder")).toBe(
    "Имя пользователя…",
  );
});
it("submits only the selected role and refreshes the named account", async () => {
  let serverRole = "user";
  const post = vi.fn((url, options) => {
    serverRole = JSON.parse(options.body).role;
    return json({ ok: true });
  });
  setup("users", (url, options) =>
    options.method === "POST"
      ? post(url, options)
      : json(
          url.includes("/moderation/users?")
            ? {
                users: [{ ...target, role: serverRole }],
                total: 1,
                has_more: false,
              }
            : { ...target, role: serverRole },
        ),
  );
  const dialog = await openUser();
  await choose("Роль Fixture user", "Модератор");
  fireEvent.click(
    within(dialog).getByRole("button", { name: "Изменить роль", exact: true }),
  );
  expect(post).not.toHaveBeenCalled();
  fireEvent.click(
    within(dialog).getByRole("button", { name: "Подтвердить", exact: true }),
  );
  await within(dialog).findByText("Изменения сохранены");
  expect(post.mock.calls[0][0]).toBe("/api/admin/users/1002/role");
  expect(JSON.parse(post.mock.calls[0][1].body)).toEqual({ role: "moderator" });
  expect(
    within(dialog).getByRole("combobox", { name: "Роль Fixture user" })
      .textContent,
  ).toBe("Модератор");
});
it("does not expose actions when account details fail to load", async () => {
  setup("users&user=1002", (url) =>
    url.includes("/moderation/users?")
      ? json({ users: [target], total: 1, has_more: false })
      : json({ error: "Access denied" }, 403),
  );
  const dialog = await screen.findByRole("dialog", {
    name: "Карточка пользователя",
  });
  await within(dialog).findByText("Access denied");
  expect(
    within(dialog).queryByRole("button", { name: "Продолжить" }),
  ).toBeNull();
});

it("preserves reason, duration and focus when returning from confirmation", async () => {
  setup("users", userFetcher());
  const dialog = await openUser();
  fireEvent.change(within(dialog).getByLabelText("Причина"), {
    target: { value: "Draft moderation reason" },
  });
  fireEvent.change(within(dialog).getByLabelText("Срок, часов"), {
    target: { value: "7" },
  });
  fireEvent.click(within(dialog).getByRole("button", { name: "Продолжить" }));
  fireEvent.click(within(dialog).getByRole("button", { name: "Назад" }));
  expect(within(dialog).getByLabelText("Причина").value).toBe(
    "Draft moderation reason",
  );
  expect(within(dialog).getByLabelText("Срок, часов").value).toBe("7");
  expect(document.activeElement).toBe(
    within(dialog).getByRole("button", { name: "Продолжить" }),
  );
});
it("preserves editable draft after a failed moderation action", async () => {
  setup(
    "users",
    userFetcher(() => json({ error: "Temporary failure" }, 503)),
  );
  const dialog = await openUser();
  fireEvent.change(within(dialog).getByLabelText("Причина"), {
    target: { value: "Draft to retry" },
  });
  fireEvent.change(within(dialog).getByLabelText("Срок, часов"), {
    target: { value: "7" },
  });
  fireEvent.click(within(dialog).getByRole("button", { name: "Продолжить" }));
  fireEvent.click(within(dialog).getByRole("button", { name: "Подтвердить" }));
  await within(dialog).findByText("Temporary failure");
  fireEvent.click(within(dialog).getByRole("button", { name: "Назад" }));
  expect(within(dialog).getByLabelText("Причина").value).toBe("Draft to retry");
  expect(within(dialog).getByLabelText("Срок, часов").value).toBe("7");
});
it("makes staff account access read-only for moderators", async () => {
  setup(
    "users&user=1002",
    (url) =>
      json(
        url.includes("/moderation/users?")
          ? { users: [], total: 0 }
          : { ...target, role: "admin" },
      ),
    { role: 1, admin: false },
  );
  const dialog = await screen.findByRole("dialog", {
    name: "Карточка пользователя",
  });
  await within(dialog).findByText(/Недостаточно прав/);
  expect(within(dialog).queryByRole("combobox")).toBeNull();
  expect(
    within(dialog).queryByRole("button", { name: "Продолжить" }),
  ).toBeNull();
});
it("shows the complete console to administrators entering through the staff route", async () => {
  setup(
    "overview",
    () =>
      json({
        overview: { users_regular: 1, users_admin: 1 },
        clip_import: { categories: [] },
      }),
    { admin: false },
  );
  await screen.findByRole("heading", { name: "Платформа сегодня" });
  expect(
    screen.getByRole("button", { name: "Каталог и импорт", exact: true }),
  ).toBeTruthy();
  expect(
    await screen.findByRole("region", { name: "График: Состав сообщества" }),
  ).toBeTruthy();
});
it.each([
  ["users", "Пользователи и доступ", "Пользователи и доступ"],
  ["reports", "Очередь жалоб", "Жалобы на контент и пользователей"],
  ["import", "Каталог и импорт", "Каталог и импорт Twitch"],
])("navigates from the overview shortcut to %s", async (tab, name, heading) => {
  setup("overview", () =>
    json({
      overview: { users_regular: 1 },
      clip_import: { catalog: {}, categories: [], hourly: [], phases: [] },
      users: [],
      reports: [],
      total: 0,
    }),
  );
  const shortcut = await screen.findByRole("link", { name: new RegExp(name) });
  expect(shortcut.getAttribute("href")).toBe(`/admin?tab=${tab}`);
  fireEvent.click(shortcut);
  expect(
    await screen.findByRole("heading", { name: heading, exact: true }),
  ).toBeTruthy();
});
it("clears the applied account search from the shared clear button", async () => {
  const urls = [];
  setup("users&q=alice", (url) => {
    urls.push(url);
    return json({ users: [], total: 0 });
  });
  const input = await screen.findByRole("searchbox", {
    name: "Найти пользователя",
  });
  expect(input.value).toBe("alice");
  fireEvent.click(screen.getByRole("button", { name: "Очистить поиск" }));
  expect(input.value).toBe("");
  expect(document.activeElement).toBe(input);
  await waitFor(() =>
    expect(urls.some((url) => url.includes("q=&"))).toBe(true),
  );
});
