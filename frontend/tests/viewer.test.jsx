import React from "react";
import { it, expect, vi } from "vitest";
import { render, screen, fireEvent, waitFor } from "@testing-library/react";
import { MemoryRouter, Routes, Route, Outlet } from "react-router-dom";
import { QueryClientProvider } from "@tanstack/react-query";
import { queryClient } from "../src/shared/api/queries";
import { SessionProvider } from "../src/features/auth/Session";
import { PreferencesProvider } from "../src/shared/preferences";
import ClipPage from "../src/features/clips/ClipPage";
it("shows an empty feed instead of a missing clip error when filters exclude all clips", async () => {
  vi.spyOn(globalThis, "fetch").mockResolvedValue(
    new Response(JSON.stringify({ clips: [], has_more: false })),
  );
  render(
    <MemoryRouter initialEntries={["/feed"]}>
      <QueryClientProvider client={queryClient}>
        <SessionProvider>
          <PreferencesProvider>
            <Routes>
              <Route element={<Outlet context={{ openLogin: () => {} }} />}>
                <Route path="/feed" element={<ClipPage feed />} />
              </Route>
            </Routes>
          </PreferencesProvider>
        </SessionProvider>
      </QueryClientProvider>
    </MemoryRouter>,
  );
  await screen.findByText("Нет клипов для текущих настроек");
  expect(screen.queryByRole("alert")).toBeNull();
  expect(
    screen.queryByText("Клип не найден. Выберите другой ролик."),
  ).toBeNull();
});
it("keeps the active Twitch iframe when voting and records no fabricated playback", async () => {
  const PreviousObserver = globalThis.ResizeObserver;
  class Observer {
    constructor(callback) {
      this.callback = callback;
    }
    observe() {
      this.callback([{ contentRect: { width: 640 } }]);
    }
    disconnect() {}
  }
  vi.stubGlobal("ResizeObserver", Observer);
  localStorage.setItem("token", "fixture");
  const clip = {
    id: 1,
    twitch_clip_id: "SafeClipId",
    title: "Viewer regression",
    broadcaster_name: "Creator",
    duration: 30,
    view_count: 100,
    like_count: 0,
    dislike_count: 0,
    user_vote: null,
    tags: [],
    classification_source: "manual",
    embed_url: "https://untrusted.example.test/player",
  };
  const fetch = vi
    .spyOn(globalThis, "fetch")
    .mockImplementation(async (url, options) => {
      let body = {};
      if (url.endsWith("/auth/me"))
        body = { user: { id: 1, username: "Viewer", role: 0 } };
      else if (url.endsWith("/clips/1/like"))
        body = { ...clip, like_count: 1, user_vote: "like" };
      else if (url.endsWith("/clips/1")) body = clip;
      else if (url.includes("/related")) body = { clips: [] };
      else if (url.includes("/comments"))
        body = { comments: [], has_more: false };
      else if (url.includes("/ads/serve")) body = { ad: null };
      return new Response(JSON.stringify(body));
    });
  try {
    render(
      <MemoryRouter initialEntries={["/clip/1"]}>
        <QueryClientProvider client={queryClient}>
          <SessionProvider>
            <PreferencesProvider>
              <Routes>
                <Route element={<Outlet context={{ openLogin: () => {} }} />}>
                  <Route path="/clip/:id" element={<ClipPage />} />
                </Route>
              </Routes>
            </PreferencesProvider>
          </SessionProvider>
        </QueryClientProvider>
      </MemoryRouter>,
    );
    await waitFor(() => expect(document.querySelector("iframe")).toBeTruthy());
    const iframe = document.querySelector("iframe");
    expect(iframe.src.startsWith("https://clips.twitch.tv/embed?")).toBe(true);
    expect(iframe.src).toContain("autoplay=false");
    await waitFor(() =>
      expect(
        screen.getByRole("textbox", { name: "Ваш комментарий" }),
      ).toBeTruthy(),
    );
    fireEvent.click(
      screen.getByRole("button", { name: "Нравится", exact: true }),
    );
    await waitFor(() =>
      expect(
        screen
          .getByRole("button", { name: "Нравится", exact: true })
          .getAttribute("aria-pressed"),
      ).toBe("true"),
    );
    expect(document.querySelector("iframe")).toBe(iframe);
    expect(
      fetch.mock.calls.filter(([url]) => url.includes("/engagement")),
    ).toHaveLength(0);
    expect(
      fetch.mock.calls.filter(([url]) => url.endsWith("/view")),
    ).toHaveLength(1);
  } finally {
    vi.stubGlobal("ResizeObserver", PreviousObserver);
  }
});
