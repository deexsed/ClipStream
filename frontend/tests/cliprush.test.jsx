import React from "react";
import { it, expect, vi, beforeEach, afterEach } from "vitest";
import { render, screen, fireEvent, waitFor } from "@testing-library/react";
import { MemoryRouter, Routes, Route, Outlet } from "react-router-dom";
import { QueryClientProvider } from "@tanstack/react-query";
import { queryClient } from "../src/shared/api/queries";
import { SessionProvider } from "../src/features/auth/Session";
import { PreferencesProvider } from "../src/shared/preferences";
import ClipRushPage from "../src/features/clips/ClipRushPage";
let resizers = [];

beforeEach(() => {
  resizers = [];
  vi.stubGlobal(
    "IntersectionObserver",
    class {
      observe() {}
      disconnect() {}
    },
  );
  vi.stubGlobal(
    "ResizeObserver",
    class {
      constructor(callback) {
        this.callback = callback;
        resizers.push(this);
      }
      observe(element) {
        this.element = element;
        this.callback([{ contentRect: { width: 600 } }]);
      }
      disconnect() {}
    },
  );
  vi.stubGlobal("matchMedia", () => ({ matches: false }));
  Element.prototype.scrollIntoView = vi.fn();
});
afterEach(() => vi.unstubAllGlobals());
function show() {
  return render(
    <MemoryRouter initialEntries={["/cliprush"]}>
      <QueryClientProvider client={queryClient}>
        <SessionProvider>
          <PreferencesProvider>
            <Routes>
              <Route element={<Outlet context={{ openLogin: vi.fn() }} />}>
                <Route path="/cliprush" element={<ClipRushPage />} />
              </Route>
            </Routes>
          </PreferencesProvider>
        </SessionProvider>
      </QueryClientProvider>
    </MemoryRouter>,
  );
}
const clips = [1, 2].map((id) => ({
  id,
  twitch_clip_id: "rush-" + id,
  title: "Rush clip " + id,
  broadcaster_name: "Creator",
  broadcaster_id: "123",
  like_count: 0,
  view_count: 100,
  tags: [],
}));
it("keeps one autoplay player, preserves it on vote, and supports navigation and comments", async () => {
  localStorage.setItem("token", "fixture");
  const fetch = vi
    .spyOn(globalThis, "fetch")
    .mockImplementation(async (url, options) => {
      let data = {};
      if (url.endsWith("/auth/me"))
        data = { user: { id: 1, username: "Viewer", role: 0 } };
      else if (url.includes("/clips?")) data = { clips, has_more: false };
      else if (url.endsWith("/like"))
        data = { like_count: 1, user_vote: "like" };
      else if (url.includes("/comments"))
        data = { comments: [], has_more: false };
      else if (url.includes("/ads/serve")) data = { ad: null };
      else if (/\/clips\/[12]$/.test(url)) data = clips[Number(url.at(-1)) - 1];
      return new Response(JSON.stringify(data));
    });
  const { container } = show();
  await screen.findByRole("heading", { name: "Rush clip 1" });
  await waitFor(() =>
    expect(container.querySelectorAll("iframe").length).toBe(1),
  );
  const player = container.querySelector("iframe");
  expect(player.src).toContain("autoplay=true");
  expect(player.src).toContain("muted=true");
  fireEvent.click(screen.getByRole("button", { name: "0" }));
  await screen.findByRole("button", { name: "1" });
  expect(container.querySelector("iframe")).toBe(player);
  fireEvent.click(screen.getByRole("button", { name: "Следующий клип" }));
  await screen.findByRole("heading", { name: "Rush clip 2" });
  await waitFor(() =>
    expect(container.querySelectorAll("iframe").length).toBe(1),
  );
  expect(container.querySelector("iframe").src).toContain("rush-2");
  const secondPlayer = container.querySelector("iframe");
  fireEvent.click(screen.getByRole("button", { name: "Обсудить" }));
  await screen.findByRole("textbox", { name: "Ваш комментарий" });
  expect(container.querySelector("iframe")).toBe(secondPlayer);
  expect(
    fetch.mock.calls.some(([url]) => url.includes("/clips/2/comments")),
  ).toBe(true);
  expect(
    screen.getByRole("complementary", { name: "Комментарии клипа" }),
  ).toBeTruthy();
  expect(screen.queryByRole("dialog", { name: "Комментарии" })).toBeNull();
  fireEvent.click(screen.getByRole("button", { name: "Закрыть обсуждение" }));
  Element.prototype.scrollIntoView.mockClear();
  resizers
    .find(
      (observer) =>
        observer.element.isConnected &&
        observer.element.classList.contains("rush-scroll"),
    )
    .callback();
  expect(
    Element.prototype.scrollIntoView.mock.contexts.at(-1).dataset.clip,
  ).toBe("2");
  fireEvent.keyDown(screen.getByLabelText("Вертикальная лента клипов"), {
    key: "ArrowUp",
  });
  await screen.findByRole("heading", { name: "Rush clip 1" });
  expect(
    fetch.mock.calls.filter(([url]) => url.endsWith("/clips/1/view")).length,
  ).toBe(1);
  expect(fetch.mock.calls.some(([url]) => url.includes("/engagement"))).toBe(
    false,
  );
});
it("renders an empty feed without mounting a player or recording an opening", async () => {
  const fetch = vi
    .spyOn(globalThis, "fetch")
    .mockImplementation(
      async () => new Response(JSON.stringify({ clips: [], has_more: false })),
    );
  const { container } = show();
  await screen.findByText("В этой подборке пока нет клипов");
  expect(container.querySelector("iframe")).toBeNull();
  expect(fetch.mock.calls.some(([url]) => url.endsWith("/view"))).toBe(false);
});

it("opens comments in a mobile bottom sheet with native dismissal", async () => {
  vi.stubGlobal("matchMedia", (query) => ({
    matches: query.includes("max-width"),
    addEventListener: vi.fn(),
    removeEventListener: vi.fn(),
  }));
  vi.spyOn(globalThis, "fetch").mockImplementation(
    async (url) =>
      new Response(
        JSON.stringify(
          url.includes("/clips?")
            ? { clips, has_more: false }
            : url.endsWith("/clips/1")
              ? clips[0]
              : url.includes("/comments")
                ? { comments: [], has_more: false }
                : { ad: null },
        ),
      ),
  );
  show();
  await screen.findByRole("heading", { name: "Rush clip 1" });
  fireEvent.click(screen.getByRole("button", { name: "Обсудить" }));
  const sheet = await screen.findByRole("dialog", { name: "Комментарии" });
  expect(sheet.classList.contains("rush-comments-sheet")).toBe(true);
  expect(
    screen.queryByRole("complementary", { name: "Комментарии клипа" }),
  ).toBeNull();
  fireEvent.click(screen.getByRole("button", { name: "Закрыть" }));
  await waitFor(() => expect(sheet.open).toBe(false));
});
