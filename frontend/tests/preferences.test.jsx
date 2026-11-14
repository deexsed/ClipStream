import React from "react";
import { describe, expect, it, vi } from "vitest";
import { fireEvent, render, screen, within } from "@testing-library/react";
import { MemoryRouter, Route, Routes } from "react-router-dom";
import Shell from "../src/app/Shell";
import { QueryClient, QueryClientProvider } from "@tanstack/react-query";

vi.mock("../src/features/auth/Session", () => ({
  useSession: () => ({ authenticated: false, user: null }),
}));
vi.mock("../src/shared/api/queries", () => ({
  useApi: () => ({ data: {}, isPending: false }),
  usePages: () => ({ data: { pages: [] }, isPending: false }),
  pageItems: () => [],
}));
vi.mock("../src/features/auth/LoginDialog", () => ({ default: () => null }));

function mount() {
  vi.stubGlobal("matchMedia", () => ({
    matches: false,
    addEventListener() {},
    removeEventListener() {},
  }));
  HTMLElement.prototype.scrollTo = vi.fn();
  return render(
    <QueryClientProvider client={new QueryClient()}>
      <MemoryRouter>
        <Routes>
          <Route element={<Shell />}>
            <Route path="*" element={<p>Клипы</p>} />
          </Route>
        </Routes>
      </MemoryRouter>
    </QueryClientProvider>,
  );
}

describe("appearance and clip language preferences", () => {
  it("toggles and restores the chosen theme after remounting", () => {
    const first = mount();
    fireEvent.click(
      screen.getByRole("button", { name: "Включить светлую тему" }),
    );
    expect(document.documentElement.dataset.theme).toBe("light");
    expect(localStorage.getItem("clipstream.theme")).toBe("light");
    first.unmount();
    mount();
    fireEvent.click(
      screen.getByRole("button", { name: "Включить тёмную тему" }),
    );
    expect(document.documentElement.dataset.theme).toBe("dark");
    expect(localStorage.getItem("clipstream.theme")).toBe("dark");
  });

  it("searches clip languages and keeps hidden selections removable", () => {
    mount();
    const trigger = screen.getByRole("button", { name: "Языки клипов" });
    expect(trigger.textContent).toContain("Языки клипов");
    fireEvent.click(trigger);
    const dialog = within(screen.getByRole("dialog", { name: "Языки клипов" }));
    const search = dialog.getByRole("searchbox", { name: "Найти язык" });
    fireEvent.change(search, { target: { value: " RU " } });
    expect(dialog.getAllByRole("checkbox")).toHaveLength(1);
    fireEvent.click(dialog.getByRole("checkbox", { name: /Русский/ }));
    expect(localStorage.getItem("clipstream.languages")).toBe('["ru"]');
    fireEvent.change(search, { target: { value: "English" } });
    fireEvent.click(dialog.getByRole("checkbox", { name: /English/ }));
    fireEvent.click(
      dialog.getByRole("button", { name: "Убрать язык: Русский" }),
    );
    expect(localStorage.getItem("clipstream.languages")).toBe('["en"]');
    fireEvent.click(dialog.getByRole("button", { name: "Все языки" }));
    expect(localStorage.getItem("clipstream.languages")).toBe("[]");
    fireEvent.change(search, { target: { value: "missing" } });
    expect(dialog.queryAllByRole("checkbox")).toHaveLength(0);
    expect(dialog.getByText(/Язык не найден/)).toBeTruthy();
  });
});
