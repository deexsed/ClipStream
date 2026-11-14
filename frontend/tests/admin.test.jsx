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
function setup(tab, fetcher) {
  localStorage.setItem("token", "fixture");
  vi.spyOn(globalThis, "fetch").mockImplementation(async (url, options) =>
    url.endsWith("/auth/me")
      ? json({ user: { id: 1, username: "Admin", role: 2 } })
      : fetcher(url, options),
  );
  render(
    <MemoryRouter initialEntries={["/admin?tab=" + tab]}>
      <QueryClientProvider client={queryClient}>
        <SessionProvider>
          <Routes>
            <Route element={<Outlet context={{ openLogin: () => {} }} />}>
              <Route path="/admin" element={<ConsolePage admin />} />
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
it("requires an explicit confirmation before changing account access and retains a failed action", async () => {
  let submissions = 0;
  setup("moderation", async (url, options) => {
    if (options.method === "POST") {
      submissions++;
      return json({ error: "Temporary service failure" }, 503);
    }
    return json({});
  });
  await screen.findByRole("heading", { name: "Доступ и ограничения" });
  const form = document.querySelector("form.surface.form-grid");
  fireEvent.change(form.querySelector("[name=id]"), {
    target: { value: "1002" },
  });
  fireEvent.change(form.querySelector("[name=reason]"), {
    target: { value: "Fixture moderation reason" },
  });
  fireEvent.submit(form);
  expect(submissions).toBe(0);
  const dialog = screen.getByRole("dialog", {
    name: "Подтвердить изменение доступа",
  });
  fireEvent.click(within(dialog).getByRole("button", { name: "Подтвердить" }));
  await within(dialog).findByText("Temporary service failure");
  expect(submissions).toBe(1);
  expect(screen.getByRole("dialog").open).toBe(true);
  expect(screen.queryByText("Ограничение обновлено")).toBeNull();
});
