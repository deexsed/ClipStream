import React, { createRef } from "react";
import { it, expect, vi } from "vitest";
import { render, screen, fireEvent, act } from "@testing-library/react";
import {
  Input,
  TextArea,
  Field,
  Checkbox,
} from "../src/shared/ui/FormControls";
import { Button } from "../src/shared/ui/Primitives";
import ConfirmDialog from "../src/shared/ui/Confirmation";
import DataTable from "../src/shared/ui/DataTable";
import SectionNav from "../src/shared/ui/SectionNav";
import ExternalLink from "../src/shared/ui/ExternalLink";

it("keeps native field refs, validation and form submission", () => {
  const ref = createRef(),
    submitted = vi.fn((event) => event.preventDefault());
  const { container } = render(
    <form onSubmit={submitted}>
      <Field>
        Name
        <Input ref={ref} name="name" required defaultValue="Alice" />
      </Field>
      <Field>
        Reason
        <TextArea name="reason" defaultValue="Details" />
      </Field>
      <Checkbox name="enabled" defaultChecked>
        Enabled
      </Checkbox>
      <Button type="submit">Save</Button>
      <Button variant="plain">Auxiliary</Button>
    </form>,
  );
  expect(ref.current).toBe(screen.getByLabelText("Name"));
  expect(ref.current.required).toBe(true);
  expect(
    Object.fromEntries(new FormData(container.querySelector("form"))),
  ).toEqual({ name: "Alice", reason: "Details", enabled: "on" });
  fireEvent.click(screen.getByText("Auxiliary"));
  expect(submitted).not.toHaveBeenCalled();
  fireEvent.click(screen.getByText("Save"));
  expect(submitted).toHaveBeenCalledOnce();
  fireEvent.click(screen.getByLabelText("Enabled"));
  expect(new FormData(container.querySelector("form")).has("enabled")).toBe(
    false,
  );
});

it("locks confirmation and dismissal during a request and exposes retry after error", () => {
  const onClose = vi.fn(),
    onConfirm = vi.fn();
  const view = render(
    <ConfirmDialog
      open
      title="Confirm"
      pending
      onClose={onClose}
      onConfirm={onConfirm}
    >
      Reason
    </ConfirmDialog>,
  );
  const dialog = screen.getByRole("dialog");
  fireEvent(dialog, new Event("cancel", { cancelable: true }));
  fireEvent.click(screen.getByRole("button", { name: "Закрыть" }));
  fireEvent.click(screen.getByText("Подтвердить"));
  expect(onClose).not.toHaveBeenCalled();
  expect(onConfirm).not.toHaveBeenCalled();
  view.rerender(
    <ConfirmDialog
      open
      title="Confirm"
      onClose={onClose}
      onConfirm={onConfirm}
      error={new Error("Try again")}
    >
      Reason
    </ConfirmDialog>,
  );
  expect(screen.getByRole("alert").textContent).toContain("Try again");
  fireEvent.click(screen.getByText("Подтвердить"));
  expect(onConfirm).toHaveBeenCalledOnce();
});

it("makes only overflowing tables keyboard scroll regions and responds to resizing", () => {
  let measure;
  vi.spyOn(globalThis, "ResizeObserver").mockImplementation(
    class {
      constructor(callback) {
        measure = callback;
      }
      observe() {}
      disconnect() {}
    },
  );
  const { container } = render(
    <DataTable label="Users">
      <thead>
        <tr>
          <th>Name</th>
        </tr>
      </thead>
      <tbody>
        <tr>
          <td>Alice</td>
        </tr>
      </tbody>
    </DataTable>,
  );
  const wrapper = container.firstChild;
  expect(wrapper.hasAttribute("tabindex")).toBe(false);
  Object.defineProperty(wrapper, "clientWidth", {
    configurable: true,
    value: 200,
  });
  Object.defineProperty(wrapper, "scrollWidth", {
    configurable: true,
    value: 600,
  });
  // A real ResizeObserver callback runs after a layout change.
  {
    act(() => measure());
    expect(screen.getByRole("region", { name: "Users" }).tabIndex).toBe(0);
    expect(screen.getByRole("table", { name: "Users" })).toBeTruthy();
    Object.defineProperty(wrapper, "clientWidth", {
      configurable: true,
      value: 600,
    });
    act(() => measure());
    expect(wrapper.hasAttribute("tabindex")).toBe(false);
  }
});

it("shares section selection semantics without submitting a surrounding form", () => {
  const onChange = vi.fn(),
    onSubmit = vi.fn();
  render(
    <form onSubmit={onSubmit}>
      <SectionNav
        label="Sections"
        items={[
          ["first", "First"],
          ["second", "Second"],
        ]}
        value="first"
        onChange={onChange}
      />
    </form>,
  );
  expect(
    screen.getByRole("button", { name: "First" }).getAttribute("aria-current"),
  ).toBe("page");
  fireEvent.click(screen.getByRole("button", { name: "Second" }));
  expect(onChange).toHaveBeenCalledWith("second");
  expect(onSubmit).not.toHaveBeenCalled();
});

it("preserves sponsored relationships on shared external links", () => {
  render(
    <ExternalLink href="https://example.test" rel="sponsored noopener">
      Campaign
    </ExternalLink>,
  );
  const link = screen.getByRole("link");
  expect(link.target).toBe("_blank");
  expect(new Set(link.rel.split(" "))).toEqual(
    new Set(["sponsored", "noopener", "noreferrer"]),
  );
});
