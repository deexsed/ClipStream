import React from "react";
import { beforeEach, describe, expect, it, vi } from "vitest";
import { act, fireEvent, render, screen, within } from "@testing-library/react";
import { MemoryRouter, Route, Routes } from "react-router-dom";
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

let media;
let resize;
beforeEach(() => {
  Object.defineProperty(HTMLElement.prototype, "scrollTo", {
    configurable: true,
    value: vi.fn(),
  });
  media = {
    matches: false,
    addEventListener: vi.fn((_, fn) => {
      resize = fn;
    }),
    removeEventListener: vi.fn(),
  };
  vi.stubGlobal("matchMedia", () => media);
});

function mount(mobile = false) {
  media.matches = mobile;
  return render(
    <QueryClientProvider client={new QueryClient()}>
      <MemoryRouter>
        <Routes>
          <Route element={<Shell />}>
            <Route path="*" element={<p>Содержимое</p>} />
          </Route>
        </Routes>
      </MemoryRouter>
    </QueryClientProvider>,
  );
}

function swipe(element, from, to, { cancel = false, multiple = false } = {}) {
  const touch = ([clientX, clientY]) => ({ clientX, clientY });
  fireEvent.touchStart(element, { touches: [touch(from)] });
  fireEvent.touchMove(element, {
    touches: multiple ? [touch(to), touch(to)] : [touch(to)],
  });
  if (cancel) fireEvent.touchCancel(element);
  fireEvent.touchEnd(element, { touches: [], changedTouches: [touch(to)] });
}

describe("sidebar navigation", () => {
  it("keeps legal documents reachable from the mobile menu", () => {
    mount(true);
    fireEvent.click(screen.getByRole("button", { name: /боковую панель/ }));
    const menu = screen.getByRole("dialog", { name: "Навигация" });
    const legal = within(menu).getByRole("link", {
      name: "Документы и контакты",
    });
    expect(legal.getAttribute("href")).toBe("/legal");
  });

  it.each([false, true])(
    "uses Главная and omits the removed watch tab (mobile: %s)",
    (mobile) => {
      const view = mount(mobile);
      if (mobile)
        fireEvent.click(
          screen.getByRole("button", { name: "Развернуть боковую панель" }),
        );
      const homeLinks = screen.getAllByRole("link", { name: "Главная" });
      expect(homeLinks.length).toBeGreaterThan(0);
      for (const link of homeLinks) expect(link.getAttribute("href")).toBe("/");
      expect(
        screen.queryByRole("link", { name: "Обзор", exact: true }),
      ).toBeNull();
      expect(
        screen.queryByRole("link", { name: "Смотреть", exact: true }),
      ).toBeNull();
      expect(view.container.querySelector('a[href="/feed"]')).toBeNull();
    },
  );

  it("does not turn a mobile vertical scroll into a closing swipe", () => {
    const view = mount(true);
    fireEvent.click(
      screen.getByRole("button", { name: "Развернуть боковую панель" }),
    );
    const content = view.container.querySelector(".sidebar-content");
    fireEvent.touchStart(content, {
      touches: [{ clientX: 180, clientY: 100 }],
    });
    fireEvent.touchMove(content, { touches: [{ clientX: 176, clientY: 116 }] });
    fireEvent.touchMove(content, { touches: [{ clientX: 50, clientY: 118 }] });
    fireEvent.touchEnd(content, {
      touches: [],
      changedTouches: [{ clientX: 50, clientY: 118 }],
    });
    expect(screen.getByRole("dialog", { name: "Навигация" }).open).toBe(true);
    swipe(content, [180, 100], [40, 103], { cancel: true });
    swipe(content, [180, 100], [40, 103], { multiple: true });
    expect(screen.getByRole("dialog", { name: "Навигация" }).open).toBe(true);
  });

  it("reverses mobile swipes during entry and exit without a stale close", async () => {
    vi.stubGlobal("matchMedia", (query) =>
      query === "(max-width: 1000px)" ? media : { matches: false },
    );
    const view = mount(true);
    const dialog = view.container.querySelector(".sidebar-drawer");
    const motions = [];
    dialog.animate = vi.fn(() => {
      let finish;
      const motion = {
        finished: new Promise((resolve) => {
          finish = resolve;
        }),
        cancel: vi.fn(),
        finish: () => finish(),
      };
      motions.push(motion);
      return motion;
    });
    swipe(
      view.container.querySelector(".sidebar-swipe-edge"),
      [5, 100],
      [130, 103],
    );
    const content = view.container.querySelector(".sidebar-content");
    swipe(content, [180, 100], [40, 103]);
    expect(dialog.dataset.state).toBe("closing");
    expect(dialog.open).toBe(true);
    swipe(content, [20, 100], [160, 103]);
    expect(dialog.dataset.state).toBe("open");
    expect(motions[0].cancel).toHaveBeenCalled();
    expect(motions[1].cancel).toHaveBeenCalled();
    await act(async () => {
      motions[0].finish();
      motions[1].finish();
    });
    expect(dialog.open).toBe(true);
    fireEvent.click(dialog);
    expect(dialog.dataset.state).toBe("closing");
    await act(async () => {
      motions[2].finish();
    });
    expect(dialog.open).toBe(true);
    await act(async () => {
      motions[3].finish();
    });
    expect(dialog.open).toBe(false);
  });

  it("does not follow a navigation link after a mobile swipe", () => {
    mount(true);
    fireEvent.click(
      screen.getByRole("button", { name: "Развернуть боковую панель" }),
    );
    const dialog = screen.getByRole("dialog", { name: "Навигация" });
    const link = within(dialog).getByRole("link", { name: "Категории" });
    swipe(link, [40, 100], [170, 103]);
    fireEvent.click(link, { detail: 1 });
    expect(dialog.open).toBe(true);
    fireEvent.click(dialog);
    expect(dialog.open).toBe(false);
  });

  it.each([5, 6])(
    "persists the final choice after %i rapid toggles",
    (count) => {
      let view = mount();
      const button = screen.getByRole("button", {
        name: "Свернуть боковую панель",
      });
      const details = view.container.querySelector(".sidebar-details");
      for (let index = 0; index < count; index++) {
        fireEvent.click(button);
        const compact = index % 2 === 0;
        expect(button.getAttribute("aria-expanded")).toBe(String(!compact));
        expect(details.hasAttribute("inert")).toBe(compact);
        expect(localStorage.getItem("clipstream.sidebar.compact")).toBe(
          String(compact),
        );
      }
      const compact = count % 2 === 1;
      view.unmount();
      view = mount();
      expect(Boolean(view.container.querySelector(".sidebar-compact"))).toBe(
        compact,
      );
      expect(
        screen
          .getByRole("button", { name: /боковую панель/ })
          .getAttribute("aria-expanded"),
      ).toBe(String(!compact));
    },
  );

  it("keeps the last swipe when it interrupts repeated button toggles", () => {
    let view = mount();
    const button = screen.getByRole("button", {
      name: "Свернуть боковую панель",
    });
    const content = view.container.querySelector(".sidebar-content");
    fireEvent.click(button);
    fireEvent.click(button);
    swipe(content, [180, 100], [50, 103]);
    expect(localStorage.getItem("clipstream.sidebar.compact")).toBe("true");
    view.unmount();
    view = mount();
    expect(view.container.querySelector(".sidebar-compact")).toBeTruthy();
  });

  it("restores both desktop states after remounting", () => {
    let view = mount();
    fireEvent.click(
      screen.getByRole("button", { name: "Свернуть боковую панель" }),
    );
    view.unmount();
    view = mount();
    expect(view.container.querySelector(".sidebar-compact")).toBeTruthy();
    fireEvent.click(
      screen.getByRole("button", { name: "Развернуть боковую панель" }),
    );
    view.unmount();
    view = mount();
    expect(view.container.querySelector(".sidebar-compact")).toBeNull();
  });

  it("persists swipes and keeps the mobile drawer closed after remounting", () => {
    let view = mount();
    swipe(
      view.container.querySelector(".sidebar-content"),
      [180, 100],
      [50, 103],
    );
    view.unmount();
    view = mount(true);
    fireEvent.click(
      screen.getByRole("button", { name: "Развернуть боковую панель" }),
    );
    view.unmount();
    view = mount(true);
    expect(screen.queryByRole("dialog", { name: "Навигация" })).toBeNull();
    view.unmount();
    view = mount();
    expect(view.container.querySelector(".sidebar-compact")).toBeTruthy();
  });

  it("defaults to expanded for an invalid saved value", () => {
    localStorage.setItem("clipstream.sidebar.compact", "invalid");
    const { container } = mount();
    expect(container.querySelector(".sidebar-compact")).toBeNull();
  });

  it("remains usable when browser storage is blocked", () => {
    vi.spyOn(Storage.prototype, "getItem").mockImplementation(() => {
      throw new DOMException("Blocked", "SecurityError");
    });
    vi.spyOn(Storage.prototype, "setItem").mockImplementation(() => {
      throw new DOMException("Blocked", "SecurityError");
    });
    const { container } = mount();
    fireEvent.click(
      screen.getByRole("button", { name: "Свернуть боковую панель" }),
    );
    expect(container.querySelector(".sidebar-compact")).toBeTruthy();
  });

  it("shows ClipStream without a dot and toggles the desktop rail", () => {
    const { container } = mount();
    expect(
      screen.getByRole("link", { name: "ClipStream — главная" }).textContent,
    ).toBe("ClipStream");
    fireEvent.click(
      screen.getByRole("button", { name: "Свернуть боковую панель" }),
    );
    expect(
      container
        .querySelector(".app-shell")
        .classList.contains("sidebar-compact"),
    ).toBe(true);
    expect(
      screen
        .getByRole("button", { name: "Развернуть боковую панель" })
        .getAttribute("aria-expanded"),
    ).toBe("false");
    expect(
      within(screen.getByRole("complementary")).getByRole("link", {
        name: "Категории",
      }),
    ).toBeTruthy();
    const details = container.querySelector(".sidebar-details");
    expect(details.hasAttribute("inert")).toBe(true);
    expect(details.getAttribute("aria-hidden")).toBe("true");
    const legal = screen.getByRole("link", { name: "Документы и контакты" });
    expect(legal.getAttribute("href")).toBe("/legal");
    expect(legal.closest("[inert]")).toBeNull();
    fireEvent.click(
      screen.getByRole("button", { name: "Развернуть боковую панель" }),
    );
    expect(container.querySelector(".sidebar-compact")).toBeNull();
    expect(details.hasAttribute("inert")).toBe(false);
    expect(
      screen.getByRole("link", { name: "Документы и контакты" }),
    ).toBeTruthy();
  });

  it("collapses and expands the rail by horizontal swipe", () => {
    const { container } = mount();
    const content = container.querySelector(".sidebar-content");
    swipe(content, [180, 100], [50, 103]);
    expect(container.querySelector(".sidebar-compact")).toBeTruthy();
    swipe(content, [20, 100], [150, 103]);
    expect(container.querySelector(".sidebar-compact")).toBeNull();
  });

  it("ignores scrolling, short, cancelled and multitouch gestures", () => {
    const { container } = mount();
    const content = container.querySelector(".sidebar-content");
    swipe(content, [180, 100], [50, 200]);
    swipe(content, [180, 100], [160, 103]);
    swipe(content, [180, 100], [50, 103], { cancel: true });
    swipe(content, [180, 100], [50, 103], { multiple: true });
    expect(container.querySelector(".sidebar-compact")).toBeNull();
  });

  it("opens from the mobile edge and closes by swipe and Escape", () => {
    const { container } = mount(true);
    expect(screen.queryByRole("dialog", { name: "Навигация" })).toBeNull();
    swipe(container.querySelector(".sidebar-swipe-edge"), [5, 100], [120, 102]);
    expect(screen.getByRole("dialog", { name: "Навигация" }).open).toBe(true);
    swipe(container.querySelector(".sidebar-content"), [180, 100], [30, 102]);
    expect(screen.queryByRole("dialog", { name: "Навигация" })).toBeNull();
    fireEvent.click(
      screen.getByRole("button", { name: "Развернуть боковую панель" }),
    );
    fireEvent(
      screen.getByRole("dialog", { name: "Навигация" }),
      new Event("cancel", { cancelable: true }),
    );
    expect(screen.queryByRole("dialog", { name: "Навигация" })).toBeNull();
  });

  it("closes the drawer on navigation and backdrop click", () => {
    mount(true);
    const open = () =>
      fireEvent.click(
        screen.getByRole("button", { name: "Развернуть боковую панель" }),
      );
    open();
    fireEvent.click(
      within(screen.getByRole("dialog", { name: "Навигация" })).getByRole(
        "link",
        { name: "Категории" },
      ),
    );
    expect(screen.queryByRole("dialog", { name: "Навигация" })).toBeNull();
    open();
    fireEvent.click(screen.getByRole("dialog", { name: "Навигация" }));
    expect(screen.queryByRole("dialog", { name: "Навигация" })).toBeNull();
  });

  it("closes the drawer on resize while preserving the desktop choice", () => {
    const { container } = mount();
    fireEvent.click(
      screen.getByRole("button", { name: "Свернуть боковую панель" }),
    );
    act(() => {
      media.matches = true;
      resize();
    });
    fireEvent.click(
      screen.getByRole("button", { name: "Развернуть боковую панель" }),
    );
    expect(screen.getByRole("dialog", { name: "Навигация" })).toBeTruthy();
    act(() => {
      media.matches = false;
      resize();
    });
    expect(screen.queryByRole("dialog", { name: "Навигация" })).toBeNull();
    expect(container.querySelector(".sidebar-compact")).toBeTruthy();
  });
});
