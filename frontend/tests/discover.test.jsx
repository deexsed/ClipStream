import React from "react";
import { it, expect, vi } from "vitest";
import {
  render,
  screen,
  fireEvent,
  waitFor,
  within,
} from "@testing-library/react";
import { MemoryRouter, useLocation } from "react-router-dom";
import { QueryClientProvider } from "@tanstack/react-query";
import { queryClient } from "../src/shared/api/queries";
import { SessionProvider } from "../src/features/auth/Session";
import { PreferencesProvider } from "../src/shared/preferences";
import DiscoverPage from "../src/features/discover/DiscoverPage";
import ClipCard from "../src/features/clips/ClipCard";

const clip = {
  id: 8,
  title: "Момент из эфира",
  broadcaster_name: "Channel",
  game_id: 1,
  game_name: "History game",
  like_count: 12,
  dislike_count: 3,
  comment_count: 7,
  view_count: 1234,
};
const json = (data) =>
  new Response(JSON.stringify(data), {
    headers: { "Content-Type": "application/json" },
  });
function Location() {
  return <output data-testid="location">{useLocation().search}</output>;
}
function setup({
  user = true,
  history = [{ id: 1, name: "History game" }],
  entries = ["/?langs=ru"],
} = {}) {
  if (user) localStorage.setItem("token", "fixture");
  const fetch = vi
    .spyOn(globalThis, "fetch")
    .mockImplementation(async (path) => {
      const url = new URL(path, "http://localhost");
      if (url.pathname.endsWith("/auth/me"))
        return json({ user: { id: 42, username: "test" } });
      if (url.pathname.endsWith("/games"))
        return json({
          games: url.searchParams.has("personalized")
            ? history
            : [{ id: 2, name: "Global game" }],
        });
      if (url.pathname.endsWith("/home-feed"))
        return json({ items: [{ clip }] });
      if (url.pathname.endsWith("/clips"))
        return json({ clips: [clip], has_more: false });
      return json({});
    });
  render(
    <MemoryRouter initialEntries={entries}>
      <QueryClientProvider client={queryClient}>
        <SessionProvider>
          <PreferencesProvider>
            <DiscoverPage />
            <Location />
          </PreferencesProvider>
        </SessionProvider>
      </QueryClientProvider>
    </MemoryRouter>,
  );
  return fetch;
}
it("uses only the signed-in viewer's history for category shortcuts", async () => {
  const fetch = setup();
  const categories = screen.getByRole("region", {
    name: "Категории из вашей истории",
  });
  const category = await within(categories).findByRole("link", {
    name: "History game",
  });
  expect(category.getAttribute("href")).toBe("/category/1");
  expect(within(categories).queryByText("Global game")).toBeNull();
  expect(
    fetch.mock.calls.some(([url]) => url.includes("/games?personalized=1")),
  ).toBe(true);
  expect(fetch.mock.calls.some(([url]) => url.includes("user_id="))).toBe(
    false,
  );
});
it("offers discovery without substituting global categories for an empty history", async () => {
  setup({ history: [] });
  await screen.findByText(
    "Посмотрите несколько клипов — здесь появятся ваши категории.",
  );
  const region = screen.getByRole("region", {
    name: "Категории из вашей истории",
  });
  expect(within(region).getAllByRole("link")).toHaveLength(1);
  expect(within(region).getByRole("link").getAttribute("href")).toBe(
    "/categories",
  );
});
it("does not request personal categories for guests", async () => {
  const fetch = setup({ user: false });
  await screen.findByText(clip.title);
  expect(screen.getByText(/Войдите и смотрите клипы/)).toBeTruthy();
  expect(fetch.mock.calls.some(([url]) => url.includes("personalized=1"))).toBe(
    false,
  );
});
it("switches feeds and returns to recommendations while retaining language preferences", async () => {
  const fetch = setup();
  await screen.findByText(clip.title);
  fireEvent.click(screen.getByRole("button", { name: "Новое", exact: true }));
  await waitFor(() =>
    expect(fetch.mock.calls.some(([url]) => url.includes("sort=recent"))).toBe(
      true,
    ),
  );
  expect(screen.getByTestId("location").textContent).toContain("langs=ru");
  fireEvent.click(screen.getByRole("button", { name: "Для вас", exact: true }));
  expect(
    screen
      .getByRole("button", { name: "Для вас", exact: true })
      .getAttribute("aria-pressed"),
  ).toBe("true");
  expect(screen.getByTestId("location").textContent).toBe("?langs=ru");
});
it("persists card density without changing the selected feed", async () => {
  setup();
  await screen.findByText(clip.title);
  fireEvent.click(screen.getByRole("button", { name: "Компактные карточки" }));
  expect(document.querySelector(".discover-page").dataset.density).toBe(
    "compact",
  );
  expect(localStorage.getItem("clipstream.discover.density")).toBe("compact");
  expect(screen.getByTestId("location").textContent).toBe("?langs=ru");
});
it("labels Twitch views separately from platform votes and does not invent unavailable comment counts", () => {
  const { rerender } = render(
    <MemoryRouter>
      <QueryClientProvider client={queryClient}>
        <ClipCard clip={clip} showStats />
      </QueryClientProvider>
    </MemoryRouter>,
  );
  expect(screen.getByLabelText("Лайки в ClipStream: 12")).toBeTruthy();
  expect(screen.getByLabelText("Дизлайки в ClipStream: 3")).toBeTruthy();
  expect(screen.getByLabelText("Просмотры на Twitch: 1234")).toBeTruthy();
  expect(screen.getByLabelText("Комментарии в ClipStream: 7")).toBeTruthy();
  rerender(
    <MemoryRouter>
      <QueryClientProvider client={queryClient}>
        <ClipCard clip={{ ...clip, comment_count: undefined }} showStats />
      </QueryClientProvider>
    </MemoryRouter>,
  );
  expect(screen.queryByLabelText(/Комментарии в ClipStream/)).toBeNull();
});
