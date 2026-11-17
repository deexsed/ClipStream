import React from "react";
import { it, expect, vi } from "vitest";
import { render, screen, fireEvent, waitFor } from "@testing-library/react";
import { MemoryRouter, Routes, Route, Outlet } from "react-router-dom";
import { QueryClientProvider } from "@tanstack/react-query";
import { queryClient } from "../src/shared/api/queries";
import { SessionProvider } from "../src/features/auth/Session";
import Comments from "../src/features/clips/Comments";

const json = (data, status = 200) =>
  new Response(JSON.stringify(data), { status });
it("keeps a failed reply draft and submits its actual parent through the keyboard", async () => {
  localStorage.setItem("token", "fixture");
  const writes = [];
  const fetch = vi
    .spyOn(globalThis, "fetch")
    .mockImplementation(async (url, options = {}) => {
      if (url.endsWith("/auth/me"))
        return json({ user: { id: 1, username: "viewer" } });
      if (options.method === "POST") {
        writes.push(JSON.parse(options.body));
        return writes.length === 1
          ? json({ error: "Service unavailable" }, 503)
          : json({ id: 9 });
      }
      return json({
        comments: [
          {
            id: 5,
            user_id: 2,
            author_name: "creator",
            body: "A real parent",
            created_at: "2026-11-14 10:00:00",
            reply_count: 0,
          },
        ],
        has_more: false,
      });
    });
  render(
    <MemoryRouter>
      <QueryClientProvider client={queryClient}>
        <SessionProvider>
          <Routes>
            <Route element={<Outlet context={{ openLogin: vi.fn() }} />}>
              <Route path="/" element={<Comments clipId={7} />} />
            </Route>
          </Routes>
        </SessionProvider>
      </QueryClientProvider>
    </MemoryRouter>,
  );
  await screen.findByText("A real parent");
  await waitFor(() =>
    expect(
      screen.queryByRole("textbox", { name: "Ваш комментарий" }),
    ).not.toBeNull(),
  );
  expect(screen.queryByRole("button", { name: "Отправить" })).toBeNull();
  fireEvent.click(screen.getByRole("button", { name: "Ответить" }));
  const field = screen.getByRole("textbox", { name: "Ваш комментарий" });
  fireEvent.change(field, { target: { value: "My reply" } });
  fireEvent.keyDown(field, { key: "Enter", ctrlKey: true });
  await screen.findByRole("alert");
  expect(field.value).toBe("My reply");
  expect(writes[0]).toEqual({ body: "My reply", parent_id: 5 });
  fireEvent.keyDown(field, { key: "Enter", ctrlKey: true });
  await screen.findByText("Комментарий отправлен");
  expect(field.value).toBe("");
  expect(writes).toHaveLength(2);
  expect(screen.queryByRole("button", { name: "Отправить" })).toBeNull();
});

it.each([
  [2, "/admin"],
  [1, "/staff"],
  [0, null],
])("routes author access only for staff role %s", async (role, path) => {
  localStorage.setItem("token", "fixture");
  vi.spyOn(globalThis, "fetch").mockImplementation(async (url) =>
    json(
      url.endsWith("/auth/me")
        ? { user: { id: 1, username: "viewer", role } }
        : {
            comments: [
              {
                id: 5,
                user_id: 2,
                author_name: "creator",
                body: "Author context",
                reply_count: 0,
              },
            ],
            has_more: false,
          },
    ),
  );
  render(
    <MemoryRouter>
      <QueryClientProvider client={queryClient}>
        <SessionProvider>
          <Routes>
            <Route element={<Outlet context={{ openLogin: vi.fn() }} />}>
              <Route path="/" element={<Comments clipId={7} />} />
            </Route>
          </Routes>
        </SessionProvider>
      </QueryClientProvider>
    </MemoryRouter>,
  );
  await screen.findByText("Author context");
  if (path)
    expect(
      (
        await screen.findByRole("link", { name: "Управлять доступом creator" })
      ).getAttribute("href"),
    ).toBe(`${path}?tab=users&user=2`);
  else
    expect(
      screen.queryByRole("link", { name: "Управлять доступом creator" }),
    ).toBeNull();
});
