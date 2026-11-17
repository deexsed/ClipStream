import React from "react";
import { expect, it, vi } from "vitest";
import {
  fireEvent,
  render,
  screen,
  waitFor,
  within,
} from "@testing-library/react";
import CategoryPicker from "../src/features/discover/CategoryPicker";
import {
  frequentCategories,
  readCategoryHistory,
  recordCategoryUse,
} from "../src/shared/categoryHistory";

const games = [
  {
    id: 1,
    name: "Just Chatting",
    box_art: "https://static-cdn.jtvnw.net/boxart/1-144x192.jpg",
  },
  { id: 2, name: "Minecraft" },
  { id: 3, name: "Counter-Strike" },
];
function mount(props = {}) {
  const changed = vi.fn();
  render(
    <>
      <button>Снаружи</button>
      <CategoryPicker
        games={games}
        viewed={[]}
        value={[]}
        onChange={changed}
        {...props}
      />
    </>,
  );
  const trigger = screen.getByRole("button", { name: /^Категория клипов:/ });
  fireEvent.click(trigger);
  return { changed, trigger };
}

it("keeps a multi-selection across searches and applies it once", () => {
  const { changed, trigger } = mount();
  const input = screen.getByRole("searchbox", { name: "Найти категорию" });
  const preview = screen
    .getByRole("button", { name: "Just Chatting" })
    .querySelector("img");
  expect(preview.getAttribute("loading")).toBe("lazy");
  fireEvent.change(input, { target: { value: "  MINE  " } });
  expect(screen.queryByRole("button", { name: "Just Chatting" })).toBeNull();
  fireEvent.click(screen.getByRole("button", { name: "Minecraft" }));
  expect(changed).not.toHaveBeenCalled();
  fireEvent.change(input, { target: { value: "counter" } });
  fireEvent.click(screen.getByRole("button", { name: "Counter-Strike" }));
  fireEvent.click(screen.getByRole("button", { name: "Применить" }));
  expect(changed).toHaveBeenCalledExactlyOnceWith(["2", "3"]);
  expect(screen.queryByRole("dialog")).toBeNull();
  expect(document.activeElement).toBe(trigger);
});

it("shows a useful empty search and clears it without dismissing the panel", () => {
  mount();
  fireEvent.change(screen.getByRole("searchbox"), {
    target: { value: "unknown" },
  });
  expect(screen.getByRole("status").textContent).toContain(
    "Категории не найдены",
  );
  fireEvent.click(screen.getByRole("button", { name: "Очистить поиск" }));
  expect(screen.getByRole("button", { name: "Minecraft" })).toBeTruthy();
  expect(screen.getByRole("dialog")).toBeTruthy();
});

it("removes a selected category from its chip while a different category is shown", () => {
  const { changed } = mount({ value: ["1", "2"] });
  fireEvent.change(screen.getByRole("searchbox"), {
    target: { value: "mine" },
  });
  expect(
    screen.queryByRole("button", { name: "Just Chatting", exact: true }),
  ).toBeNull();
  fireEvent.click(
    screen.getByRole("button", { name: "Снять выбор: Just Chatting" }),
  );
  expect(
    screen.queryByRole("button", { name: "Снять выбор: Just Chatting" }),
  ).toBeNull();
  expect(document.activeElement).toBe(
    screen.getByRole("button", { name: "Снять выбор: Minecraft" }),
  );
  expect(changed).not.toHaveBeenCalled();
  fireEvent.click(screen.getByRole("button", { name: "Применить" }));
  expect(changed).toHaveBeenCalledExactlyOnceWith(["2"]);
});

it("discards edits on cancel and clears the applied filter on confirmation", () => {
  const { changed, trigger } = mount({ value: ["2"] });
  expect(
    screen
      .getByRole("button", { name: "Minecraft" })
      .getAttribute("aria-pressed"),
  ).toBe("true");
  fireEvent.click(screen.getByRole("button", { name: "Сбросить" }));
  fireEvent(
    screen.getByRole("dialog"),
    new Event("cancel", { cancelable: true }),
  );
  expect(screen.queryByRole("dialog")).toBeNull();
  expect(changed).not.toHaveBeenCalled();
  fireEvent.click(trigger);
  expect(
    screen
      .getByRole("button", { name: "Minecraft" })
      .getAttribute("aria-pressed"),
  ).toBe("true");
  fireEvent.click(screen.getByRole("button", { name: "Сбросить" }));
  fireEvent.click(screen.getByRole("button", { name: "Применить" }));
  expect(changed).toHaveBeenCalledExactlyOnceWith([]);
  expect(screen.queryByRole("dialog")).toBeNull();
});

it("recycles offscreen cards and searches the entire catalog", async () => {
  mount({
    games: Array.from({ length: 400 }, (_, i) => ({
      id: i + 1,
      name: `Game ${i + 1}`,
    })),
  });
  const catalog = screen.getByRole("region", { name: "Каталог категорий" });
  expect(within(catalog).getAllByRole("button").length).toBeLessThan(30);
  expect(within(catalog).getByRole("button", { name: "Game 1" })).toBeTruthy();
  const viewport = catalog.closest(".category-picker-scroll");
  viewport.scrollTop = 3400;
  fireEvent.scroll(viewport);
  await waitFor(() =>
    expect(
      within(catalog).queryByRole("button", { name: "Game 1" }),
    ).toBeNull(),
  );
  expect(within(catalog).getByRole("button", { name: "Game 81" })).toBeTruthy();
  fireEvent.change(screen.getByRole("searchbox"), {
    target: { value: "Game 400" },
  });
  expect(
    screen.getByRole("button", { name: "Game 400", exact: true }),
  ).toBeTruthy();
});

it("ranks real choices and view history without duplicating catalog entries", () => {
  recordCategoryUse(2, 42);
  recordCategoryUse(2, 42);
  recordCategoryUse(1, 42, "views");
  mount({ viewer: 42, viewed: [{ id: 3, watched_count: 4 }] });
  const frequent = screen.getByRole("region", {
    name: "Часто выбираете и смотрите",
  });
  expect(
    within(frequent)
      .getAllByRole("button")
      .map((el) => el.textContent),
  ).toEqual(["Counter-Strike", "Minecraft", "Just Chatting"]);
  expect(screen.getAllByRole("button", { name: "Minecraft" })).toHaveLength(1);
  expect(readCategoryHistory(42).find((row) => row.id === "2").selections).toBe(
    2,
  );
  expect(readCategoryHistory(43)).toEqual([]);
  expect(readCategoryHistory()).toEqual([]);
});

it("tolerates malformed or unavailable storage and limits history", () => {
  localStorage.setItem("clipstream.category-history.v1.guest", "{bad json");
  expect(readCategoryHistory()).toEqual([]);
  for (let id = 1; id <= 70; id++) recordCategoryUse(id);
  expect(readCategoryHistory()).toHaveLength(64);
  recordCategoryUse("__proto__");
  expect(readCategoryHistory()).toHaveLength(64);
  vi.spyOn(Storage.prototype, "getItem").mockImplementation(() => {
    throw Error("blocked");
  });
  vi.spyOn(Storage.prototype, "setItem").mockImplementation(() => {
    throw Error("blocked");
  });
  expect(() => recordCategoryUse(1)).not.toThrow();
  expect(frequentCategories(games, [], undefined)).toEqual([]);
});

it("keeps loading and API failures explicit with retry", () => {
  const retry = vi.fn();
  mount({ games: [], error: new Error("Каталог недоступен"), onRetry: retry });
  expect(screen.getByRole("alert").textContent).toContain("Каталог недоступен");
  fireEvent.click(screen.getByRole("button", { name: "Попробовать снова" }));
  expect(retry).toHaveBeenCalledOnce();
});
