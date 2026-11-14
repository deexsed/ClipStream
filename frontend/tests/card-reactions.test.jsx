import React from "react";
import { it, expect, vi } from "vitest";
import { render, screen, fireEvent, waitFor } from "@testing-library/react";
import { MemoryRouter, Routes, Route, Outlet } from "react-router-dom";
import { QueryClientProvider, useQuery } from "@tanstack/react-query";
import { queryClient } from "../src/shared/api/queries";
import { SessionProvider } from "../src/features/auth/Session";
import { PreferencesProvider } from "../src/shared/preferences";
import ClipCard from "../src/features/clips/ClipCard";
import ClipPage from "../src/features/clips/ClipPage";

const clip = {
  id: 8,
  title: "Card reaction",
  broadcaster_name: "Channel",
  like_count: 2,
  dislike_count: 1,
  user_vote: null,
  view_count: 9876,
  platform_view_count: 14,
  comment_count: 7,
  game_id: 12,
  game_name: "Counter-Strike",
};
const json = (data, status = 200) =>
  new Response(JSON.stringify(data), {
    status,
    headers: { "Content-Type": "application/json" },
  });
function CachedCard() {
  useQuery({ queryKey: ["recommendations", 42, ""], enabled: false });
  useQuery({
    queryKey: ["pages", "/clips", "clips", "limit=24"],
    enabled: false,
  });
  const { data } = useQuery({
    queryKey: ["api", "/clips/8"],
    queryFn: () => clip,
    enabled: false,
  });
  return <ClipCard clip={data} showStats />;
}
function setup({ guest = false, voteResponse, comments = false } = {}) {
  const openLogin = vi.fn();
  let current = { ...clip };
  if (!guest) localStorage.setItem("token", "fixture");
  queryClient.setQueryData(["api", "/clips/8"], clip);
  queryClient.setQueryData(["recommendations", 42, ""], {
    pages: [{ clips: [clip] }],
    pageParams: [""],
  });
  queryClient.setQueryData(["pages", "/clips", "clips", "limit=24"], {
    pages: [{ clips: [clip] }],
    pageParams: [0],
  });
  const fetch = vi
    .spyOn(globalThis, "fetch")
    .mockImplementation(async (url, options) => {
      if (url.endsWith("/auth/me"))
        return json({ user: { id: 42, username: "Viewer" } });
      if (url.endsWith("/clips/8/like")) {
        if (voteResponse) return voteResponse();
        const next = JSON.parse(options.body).is_like ? "like" : "dislike";
        if (current.user_vote === "like") current.like_count--;
        if (current.user_vote === "dislike") current.dislike_count--;
        current.user_vote = current.user_vote === next ? null : next;
        if (current.user_vote === "like") current.like_count++;
        if (current.user_vote === "dislike") current.dislike_count++;
        return json(current);
      }
      if (url.endsWith("/clips/8")) return json(clip);
      if (url.includes("/comments"))
        return json({ comments: [], has_more: false });
      if (url.includes("/related")) return json({ clips: [] });
      return json({});
    });
  render(
    <MemoryRouter initialEntries={[comments ? "/clip/8#comments" : "/"]}>
      <QueryClientProvider client={queryClient}>
        <SessionProvider>
          <PreferencesProvider>
            <Routes>
              <Route element={<Outlet context={{ openLogin }} />}>
                <Route index element={<CachedCard />} />
                <Route path="/clip/:id" element={<ClipPage />} />
              </Route>
            </Routes>
          </PreferencesProvider>
        </SessionProvider>
      </QueryClientProvider>
    </MemoryRouter>,
  );
  return { fetch, openLogin };
}
it("toggles, switches and removes votes while synchronizing cached copies", async () => {
  setup();
  await waitFor(() =>
    expect(queryClient.getQueryData(["session", "fixture"])?.user).toBeTruthy(),
  );
  fireEvent.click(
    screen.getByRole("button", { name: "Лайки в ClipStream: 2" }),
  );
  await screen.findByRole("button", {
    name: "Лайки в ClipStream: 3",
    pressed: true,
  });
  expect(
    queryClient.getQueryData(["recommendations", 42, ""]).pages[0].clips[0]
      .like_count,
  ).toBe(3);
  expect(
    queryClient.getQueryData(["pages", "/clips", "clips", "limit=24"]).pages[0]
      .clips[0].user_vote,
  ).toBe("like");
  fireEvent.click(
    screen.getByRole("button", { name: "Дизлайки в ClipStream: 1" }),
  );
  await screen.findByRole("button", {
    name: "Дизлайки в ClipStream: 2",
    pressed: true,
  });
  expect(
    screen.getByRole("button", {
      name: "Лайки в ClipStream: 2",
      pressed: false,
    }),
  ).toBeTruthy();
  fireEvent.click(
    screen.getByRole("button", { name: "Дизлайки в ClipStream: 2" }),
  );
  await screen.findByRole("button", {
    name: "Дизлайки в ClipStream: 1",
    pressed: false,
  });
});
it("opens login for guest voting without sending a vote", () => {
  const { fetch, openLogin } = setup({ guest: true });
  fireEvent.click(
    screen.getByRole("button", { name: "Лайки в ClipStream: 2" }),
  );
  expect(openLogin).toHaveBeenCalledOnce();
  expect(fetch.mock.calls.some(([url]) => url.endsWith("/like"))).toBe(false);
});
it("disables pending votes and retains counts after a server failure", async () => {
  let finish;
  const { fetch } = setup({
    voteResponse: () =>
      new Promise((resolve) => {
        finish = resolve;
      }),
  });
  await waitFor(() =>
    expect(queryClient.getQueryData(["session", "fixture"])?.user).toBeTruthy(),
  );
  fireEvent.click(
    screen.getByRole("button", { name: "Лайки в ClipStream: 2" }),
  );
  await waitFor(() => expect(finish).toBeTypeOf("function"));
  expect(
    screen.getByRole("button", { name: "Лайки в ClipStream: 2" }).disabled,
  ).toBe(true);
  expect(
    screen.getByRole("button", { name: "Дизлайки в ClipStream: 1" }).disabled,
  ).toBe(true);
  finish(json({ error: "Unavailable" }, 503));
  await screen.findByRole("alert");
  expect(
    screen.getByRole("button", {
      name: "Лайки в ClipStream: 2",
      pressed: false,
    }).disabled,
  ).toBe(false);
  expect(
    fetch.mock.calls.filter(([url]) => url.endsWith("/like")),
  ).toHaveLength(1);
});
it("distinguishes view sources and links to the discussion", () => {
  setup({ guest: true });
  expect(
    screen
      .getByRole("link", { name: "Категория: Counter-Strike" })
      .getAttribute("href"),
  ).toBe("/category/12");
  expect(
    screen
      .getByLabelText("Просмотры на Twitch: 9876")
      .querySelector("svg.lucide-twitch"),
  ).toBeTruthy();
  expect(
    screen
      .getByLabelText("Просмотры в ClipStream: 14")
      .querySelector("img")
      .getAttribute("src"),
  ).toContain("favicon-16.png");
  expect(
    screen
      .getByRole("link", { name: "Комментарии в ClipStream: 7" })
      .getAttribute("href"),
  ).toBe("/clip/8#comments");
});
it("focuses and scrolls the discussion on a direct comment link", async () => {
  const scroll = vi.spyOn(Element.prototype, "scrollIntoView");
  setup({ comments: true, guest: true });
  const discussion = await screen.findByRole("region", {
    name: "Обсуждение клипа",
  });
  expect(document.activeElement).toBe(discussion);
  expect(scroll).toHaveBeenCalledWith({ block: "start", behavior: "instant" });
});
