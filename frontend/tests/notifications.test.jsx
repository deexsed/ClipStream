import React from "react";
import { beforeEach, describe, expect, it, vi } from "vitest";
import { fireEvent, render, screen } from "@testing-library/react";
import { MemoryRouter, Route, Routes } from "react-router-dom";
import { QueryClient, QueryClientProvider } from "@tanstack/react-query";
import Shell from "../src/app/Shell";

vi.mock("../src/features/auth/Session", () => ({
  useSession: () => ({ authenticated: true, user: { username: "viewer" } }),
}));
vi.mock("../src/shared/api/queries", async (importOriginal) => ({
  ...(await importOriginal()),
  useApi: () => ({ data: {}, isPending: false }),
  usePages: () => ({ data: { pages: [] }, isPending: false }),
}));
vi.mock("../src/features/auth/LoginDialog", () => ({ default: () => null }));

beforeEach(() => {
  vi.stubGlobal("matchMedia", () => ({
    matches: false,
    addEventListener: vi.fn(),
    removeEventListener: vi.fn(),
  }));
  Object.defineProperty(HTMLElement.prototype, "scrollTo", {
    configurable: true,
    value: vi.fn(),
  });
});

function mount() {
  return render(
    <QueryClientProvider client={new QueryClient()}>
      <MemoryRouter>
        <Routes>
          <Route element={<Shell />}>
            <Route path="*" element={<button>Содержимое</button>} />
          </Route>
        </Routes>
      </MemoryRouter>
    </QueryClientProvider>,
  );
}

describe("notifications panel", () => {
  it("opens a nonmodal region and toggles closed from the bell icon", () => {
    mount();
    const trigger = screen.getByRole("button", { name: "Уведомления" });
    expect(trigger.getAttribute("aria-expanded")).toBe("false");
    fireEvent.click(trigger);
    expect(screen.getByRole("region", { name: "Уведомления" })).toBeTruthy();
    expect(screen.queryByRole("dialog", { name: "Уведомления" })).toBeNull();
    expect(trigger.getAttribute("aria-expanded")).toBe("true");
    fireEvent.click(trigger.querySelector("svg"));
    expect(screen.queryByRole("region", { name: "Уведомления" })).toBeNull();
    expect(trigger.getAttribute("aria-expanded")).toBe("false");
  });

  it("keeps inside clicks open and closes on an outside click", () => {
    mount();
    fireEvent.click(screen.getByRole("button", { name: "Уведомления" }));
    fireEvent.click(screen.getByText("Новых уведомлений пока нет."));
    expect(screen.getByRole("region", { name: "Уведомления" })).toBeTruthy();
    fireEvent.click(screen.getByRole("button", { name: "Содержимое" }));
    expect(screen.queryByRole("region", { name: "Уведомления" })).toBeNull();
  });

  it("closes with Escape or the close button and restores keyboard focus", () => {
    mount();
    const trigger = screen.getByRole("button", { name: "Уведомления" });
    fireEvent.click(trigger);
    expect(document.activeElement).toBe(
      screen.getByRole("region", { name: "Уведомления" }),
    );
    fireEvent.keyDown(document.activeElement, { key: "Escape" });
    expect(screen.queryByRole("region", { name: "Уведомления" })).toBeNull();
    expect(document.activeElement).toBe(trigger);
    fireEvent.click(trigger);
    fireEvent.click(
      screen.getByRole("button", { name: "Закрыть уведомления" }),
    );
    expect(screen.queryByRole("region", { name: "Уведомления" })).toBeNull();
    expect(document.activeElement).toBe(trigger);
  });

  it("closes when navigating from the header", () => {
    mount();
    fireEvent.click(screen.getByRole("button", { name: "Уведомления" }));
    fireEvent.click(screen.getByRole("link", { name: "Мой профиль" }));
    expect(screen.queryByRole("region", { name: "Уведомления" })).toBeNull();
  });
});
