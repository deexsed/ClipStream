import React from "react";
import { it, expect, vi } from "vitest";
import { render, screen, fireEvent, waitFor } from "@testing-library/react";
import Select from "../src/shared/ui/Select";

it("preserves named form values and supports keyboard selection", async () => {
  const changed = vi.fn();
  const { container } = render(
    <form>
      <Select
        name="sort"
        aria-label="Sort"
        defaultValue="recent"
        onChange={changed}
      >
        <option value="recent">Recent</option>
        <option value="best">Best</option>
      </Select>
    </form>,
  );
  const trigger = screen.getByRole("combobox", { name: "Sort" });
  expect(new FormData(container.querySelector("form")).get("sort")).toBe(
    "recent",
  );
  fireEvent.keyDown(trigger, { key: "ArrowDown" });
  fireEvent.click(await screen.findByRole("option", { name: "Best" }));
  await waitFor(() =>
    expect(new FormData(container.querySelector("form")).get("sort")).toBe(
      "best",
    ),
  );
  expect(changed.mock.calls[0][0].target).toEqual({
    name: "sort",
    value: "best",
  });
  expect(trigger.textContent).toBe("Best");
});

it("keeps its popup inside a modal and dismisses it without closing the modal", async () => {
  render(
    <dialog open>
      <Select aria-label="Order">
        <option value="a">First</option>
        <option value="b">Second</option>
      </Select>
    </dialog>,
  );
  const trigger = screen.getByRole("combobox");
  fireEvent.keyDown(trigger, { key: "ArrowDown" });
  const popup = await screen.findByRole("listbox");
  expect(popup.closest("dialog").open).toBe(true);
  fireEvent.keyDown(popup, { key: "Escape" });
  await waitFor(() => expect(screen.queryByRole("listbox")).toBeNull());
  expect(document.querySelector("dialog").open).toBe(true);
});
