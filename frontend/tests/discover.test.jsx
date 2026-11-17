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
import { readCategoryHistory } from "../src/shared/categoryHistory";

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

it("selects a searched category in the same feed and remembers it once", async () => {
  const fetch = setup();
  await screen.findByText(clip.title);
  fireEvent.click(screen.getByRole("button", { name: /^Категория клипов:/ }));
  fireEvent.change(screen.getByRole("searchbox", { name: "Найти категорию" }), {
    target: { value: "GLOBAL" },
  });
  fireEvent.click(
    screen.getByRole("button", { name: "Global game", exact: true }),
  );
  fireEvent.click(screen.getByRole("button", { name: "Применить" }));
  await waitFor(() =>
    expect(fetch.mock.calls.some(([url]) => url.includes("game_id=2"))).toBe(
      true,
    ),
  );
  expect(screen.getByTestId("location").textContent).toContain("langs=ru");
  expect(readCategoryHistory(42).find((row) => row.id === "2").selections).toBe(
    1,
  );
  fireEvent.click(screen.getByRole("button", { name: /^Категория клипов:/ }));
  expect(
    screen.getByRole("region", { name: "Часто выбираете и смотрите" }),
  ).toBeTruthy();
});

it("applies multiple categories in one API request and preserves the other filters", async () => {
  const fetch = setup({
    entries: ["/?langs=ru&game_id=2&period=week&sort=recent"],
  });
  await screen.findByText(clip.title);
  fireEvent.click(screen.getByRole("button", { name: /^Категория клипов:/ }));
  fireEvent.click(
    within(screen.getByRole("dialog")).getByRole("button", {
      name: "Third game",
    }),
  );
  expect(fetch.mock.calls.some(([url]) => url.includes("game_ids"))).toBe(
    false,
  );
  fireEvent.click(screen.getByRole("button", { name: "Применить" }));
  await waitFor(() => {
    const request = fetch.mock.calls
      .map(([url]) => new URL(url, "http://localhost"))
      .find((url) => url.searchParams.get("game_ids") === "2,3");
    expect(request?.searchParams.get("period")).toBe("week");
    expect(request?.searchParams.get("sort")).toBe("recent");
    expect(request?.searchParams.get("clip_langs")).toBe("ru");
    expect(request?.searchParams.has("game_id")).toBe(false);
  });
  expect(
    new URLSearchParams(screen.getByTestId("location").textContent).get(
      "game_ids",
    ),
  ).toBe("2,3");
});
function Location() {
  return <output data-testid="location">{useLocation().search}</output>;
}
function setup({
  user = true,
  mode,
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
            : [
                { id: 2, name: "Global game" },
                { id: 3, name: "Third game" },
              ],
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
            <DiscoverPage mode={mode} />
            <Location />
          </PreferencesProvider>
        </SessionProvider>
      </QueryClientProvider>
    </MemoryRouter>,
  );
  return fetch;
}
it("searches category names on the catalog page and restores the catalog on clear", async () => {
  setup({ mode: "categories", entries: ["/categories"], user: false });
  await screen.findByRole("heading", { name: "Global game" });
  const search = screen.getByRole("searchbox", { name: "Найти категорию" });
  fireEvent.change(search, { target: { value: "  THIRD " } });
  expect(screen.queryByRole("heading", { name: "Global game" })).toBeNull();
  const link = screen.getByRole("link", { name: /Third game/ });
  expect(link.getAttribute("href")).toBe("/category/3");
  fireEvent.click(screen.getByRole("button", { name: "Очистить поиск" }));
  expect(screen.getByRole("heading", { name: "Global game" })).toBeTruthy();
});
it("uses only the signed-in viewer's history for category shortcuts", async () => {
  const fetch = setup();
  const categories = await screen.findByRole("region", {
    name: "Категории из вашей истории",
  });
  const category = await within(categories).findByRole("button", {
    name: "History game",
  });
  fireEvent.click(category);
  await waitFor(() =>
    expect(screen.getByTestId("location").textContent).toContain("game_id=1"),
  );
  expect(category.getAttribute("aria-pressed")).toBe("true");
  expect(fetch.mock.calls.some(([url]) => url.includes("game_id=1"))).toBe(
    true,
  );
  expect(within(categories).queryByText("Global game")).toBeNull();
  expect(
    fetch.mock.calls.some(([url]) => url.includes("/games?personalized=1")),
  ).toBe(true);
  expect(fetch.mock.calls.some(([url]) => url.includes("user_id="))).toBe(
    false,
  );
});
it("does not invent personalized categories for an empty history", async () => {
  setup({ history: [] });
  await screen.findByText(clip.title);
  expect(
    screen.queryByRole("region", { name: "Категории из вашей истории" }),
  ).toBeNull();
  expect(
    screen.getByRole("button", { name: /^Категория клипов:/ }),
  ).toBeTruthy();
});
it("shows a neutral guest feed without personal categories or sign-in prompts", async () => {
  const fetch = setup({ user: false });
  await screen.findByText(clip.title);
  expect(
    screen.getByRole("heading", { name: "Клипы", exact: true }),
  ).toBeTruthy();
  expect(
    screen.queryByText(
      /Войдите и смотрите|Ваши категории|Персональная смесь|Клипы для вас/,
    ),
  ).toBeNull();
  expect(
    screen.queryByRole("region", { name: "Категории из вашей истории" }),
  ).toBeNull();
  expect(fetch.mock.calls.some(([url]) => url.includes("personalized=1"))).toBe(
    false,
  );
  expect(fetch.mock.calls.some(([url]) => url.includes("/home-feed"))).toBe(
    true,
  );
});
it("switches feeds and returns to recommendations while retaining language preferences", async () => {
  const fetch = setup();
  await screen.findByText(clip.title);
  expect(
    screen.queryByRole("navigation", { name: "Подборка клипов" }),
  ).toBeNull();
  fireEvent.keyDown(screen.getByRole("combobox", { name: "Сортировка" }), {
    key: "ArrowDown",
  });
  fireEvent.click(await screen.findByRole("option", { name: "Сначала новые" }));
  await waitFor(() =>
    expect(fetch.mock.calls.some(([url]) => url.includes("sort=recent"))).toBe(
      true,
    ),
  );
  expect(screen.getByTestId("location").textContent).toContain("langs=ru");
  fireEvent.click(screen.getByRole("button", { name: "Сбросить фильтры" }));
  expect(screen.getByRole("combobox", { name: "Сортировка" }).textContent).toBe(
    "Смешанная лента",
  );
  expect(screen.getByTestId("location").textContent).toBe("?langs=ru");
});
it("uses the default large cards even with a saved compact preference", async () => {
  localStorage.setItem("clipstream.discover.density", "compact");
  setup();
  await screen.findByText(clip.title);
  expect(
    document.querySelector(".discover-page").hasAttribute("data-density"),
  ).toBe(false);
  expect(
    screen.queryByRole("group", { name: "Плотность карточек" }),
  ).toBeNull();
  expect(
    screen.queryByRole("button", { name: "Компактные карточки" }),
  ).toBeNull();
  expect(screen.queryByRole("button", { name: "Крупные карточки" })).toBeNull();
  expect(screen.getByTestId("location").textContent).toBe("?langs=ru");
});
it("filters from a card's category without navigating away", async () => {
  const fetch = setup();
  await screen.findByText(clip.title);
  fireEvent.click(
    screen.getByRole("button", { name: "Категория: History game" }),
  );
  await waitFor(() =>
    expect(fetch.mock.calls.some(([url]) => url.includes("game_id=1"))).toBe(
      true,
    ),
  );
  expect(screen.getByTestId("location").textContent).toContain("game_id=1");
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

it("combines category and period filters in the same grid and restores a shared URL", async () => {
  const fetch = setup({
    entries: ["/?langs=ru&game_id=2&period=week&sort=recent"],
  });
  await screen.findByText(clip.title);
  expect(
    screen.getByRole("button", { name: /^Категория клипов:/ }).textContent,
  ).toBe("Global game");
  expect(
    screen.getByRole("combobox", { name: "Период клипов" }).textContent,
  ).toBe("Неделя");
  expect(
    fetch.mock.calls.some(
      ([url]) =>
        url.includes("game_id=2") &&
        url.includes("period=week") &&
        url.includes("sort=recent"),
    ),
  ).toBe(true);
  fireEvent.click(screen.getByRole("button", { name: "Сбросить фильтры" }));
  expect(screen.getByTestId("location").textContent).toBe("?langs=ru");
});
it("starts a fresh recommendation session only on explicit shuffle", async () => {
  const fetch = setup();
  await screen.findByText(clip.title);
  const sessions = () =>
    fetch.mock.calls
      .filter(([url]) => url.includes("/home-feed"))
      .map(([url]) =>
        new URL(url, "http://localhost").searchParams.get("session_id"),
      );
  const before = sessions().at(-1);
  fireEvent.click(screen.getByRole("button", { name: "Новая смесь" }));
  await waitFor(() => expect(sessions().at(-1)).not.toBe(before));
  expect(screen.getByTestId("location").textContent).toBe("?langs=ru");
});
