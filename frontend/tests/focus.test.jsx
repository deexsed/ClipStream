import React from "react";
import { it, expect, vi } from "vitest";
import { render, screen, fireEvent } from "@testing-library/react";
import { MemoryRouter, Routes, Route } from "react-router-dom";
import { QueryClient, QueryClientProvider } from "@tanstack/react-query";
import Shell from "../src/app/Shell";

vi.mock("../src/features/auth/Session", () => ({
  useSession: () => ({ authenticated: false, user: null }),
}));
vi.mock("../src/shared/api/queries", () => ({
  useApi: () => ({ data: {}, isPending: false }),
  usePages: () => ({ data: { pages: [] }, isPending: false }),
  pageItems: () => [],
}));
vi.mock("../src/features/auth/LoginDialog", () => ({ default: () => null }));

it("keeps pointer typing quiet, exposes keyboard focus, and never moves search focus behind a modal", () => {
  Object.defineProperty(HTMLElement.prototype, "scrollTo", {
    configurable: true,
    value: vi.fn(),
  });
  vi.stubGlobal("matchMedia", () => ({
    matches: false,
    addEventListener() {},
    removeEventListener() {},
  }));
  vi.spyOn(navigator, "platform", "get").mockReturnValue("Win32");
  render(
    <QueryClientProvider client={new QueryClient()}>
      <MemoryRouter>
        <Routes>
          <Route element={<Shell />}>
            <Route path="*" element={<p>Page</p>} />
          </Route>
        </Routes>
      </MemoryRouter>
    </QueryClientProvider>,
  );
  const search = screen.getByRole("searchbox");
  expect(document.querySelector(".header-search kbd")).toBeNull();
  fireEvent.pointerDown(search);
  search.focus();
  fireEvent.keyDown(search, { key: "a" });
  expect(document.documentElement.dataset.input).toBe("pointer");
  fireEvent.keyDown(search, { key: "Tab" });
  expect(document.documentElement.dataset.input).toBe("keyboard");
  search.blur();
  fireEvent.keyDown(document.body, { key: "л", code: "KeyK", ctrlKey: true });
  expect(document.activeElement).toBe(search);
  fireEvent.click(screen.getByRole("button", { name: "Языки клипов" }));
  const close = screen.getByRole("button", { name: "Закрыть", exact: true });
  close.focus();
  fireEvent.keyDown(close, { key: "k", code: "KeyK", ctrlKey: true });
  expect(document.activeElement).toBe(close);
  delete document.documentElement.dataset.input;
  vi.unstubAllGlobals();
});

it.each([
  ["/?q=initial", "initial"],
  ["/admin?tab=users&q=alice", ""],
])("scopes the header query to global search at %s", (entry, expected) => {
  vi.stubGlobal("matchMedia", () => ({
    matches: false,
    addEventListener() {},
    removeEventListener() {},
  }));
  render(
    <QueryClientProvider client={new QueryClient()}>
      <MemoryRouter initialEntries={[entry]}>
        <Routes>
          <Route element={<Shell />}>
            <Route path="*" element={<p>Page</p>} />
          </Route>
        </Routes>
      </MemoryRouter>
    </QueryClientProvider>,
  );
  const input = screen.getByRole("searchbox", {
    name: "Поиск клипов, каналов и людей",
  });
  expect(input.value).toBe(expected);
  if (expected) {
    fireEvent.click(screen.getByRole("button", { name: "Очистить поиск" }));
    expect(input.value).toBe("");
  } else
    expect(screen.queryByRole("button", { name: "Очистить поиск" })).toBeNull();
  vi.unstubAllGlobals();
});
