import React, { createRef, useState } from "react";
import { it, expect, vi } from "vitest";
import { render, screen, fireEvent } from "@testing-library/react";
import SearchField from "../src/shared/ui/SearchField";

it("preserves input refs and named form values without submitting on clear", () => {
  const ref = createRef();
  const submitted = vi.fn();
  const cleared = vi.fn();
  const { container } = render(
    <form onSubmit={submitted}>
      <SearchField
        ref={ref}
        name="q"
        aria-label="Поиск"
        defaultValue="original"
        onClear={cleared}
      />
    </form>,
  );
  expect(ref.current).toBe(screen.getByRole("searchbox"));
  expect(new FormData(container.querySelector("form")).get("q")).toBe(
    "original",
  );
  fireEvent.click(screen.getByRole("button", { name: "Очистить поиск" }));
  expect(ref.current.value).toBe("");
  expect(document.activeElement).toBe(ref.current);
  expect(new FormData(container.querySelector("form")).get("q")).toBe("");
  expect(submitted).not.toHaveBeenCalled();
  expect(cleared).toHaveBeenCalledOnce();
  expect(screen.queryByRole("button", { name: "Очистить поиск" })).toBeNull();
});
it("updates controlled filtering on typing and clearing", () => {
  function Controlled() {
    const [value, setValue] = useState("");
    return (
      <>
        <SearchField
          aria-label="Фильтр"
          value={value}
          onValueChange={setValue}
        />
        <output>{value || "Все результаты"}</output>
      </>
    );
  }
  render(<Controlled />);
  const input = screen.getByRole("searchbox");
  fireEvent.change(input, { target: { value: "Русский" } });
  expect(screen.getByRole("status").textContent).toBe("Русский");
  fireEvent.click(screen.getByRole("button", { name: "Очистить поиск" }));
  expect(input.value).toBe("");
  expect(screen.getByRole("status").textContent).toBe("Все результаты");
});
it("consumes Escape only when it clears a non-empty field", () => {
  const escaped = vi.fn();
  render(
    <div onKeyDown={escaped}>
      <SearchField aria-label="Поиск" defaultValue="draft" />
    </div>,
  );
  const input = screen.getByRole("searchbox");
  fireEvent.keyDown(input, { key: "Escape" });
  expect(input.value).toBe("");
  expect(escaped).not.toHaveBeenCalled();
  fireEvent.keyDown(input, { key: "Escape" });
  expect(escaped).toHaveBeenCalledOnce();
});
it("keeps disabled search immutable", () => {
  render(<SearchField aria-label="Поиск" defaultValue="locked" disabled />);
  expect(screen.getByRole("button", { name: "Очистить поиск" }).disabled).toBe(
    true,
  );
});
