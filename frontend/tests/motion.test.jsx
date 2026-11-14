import React, { StrictMode, useState } from "react";
import { act, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, expect, it, vi } from "vitest";
import { Dialog } from "../src/shared/ui/Primitives";
import Reveal from "../src/shared/ui/Reveal";
import Select from "../src/shared/ui/Select";

afterEach(() => {
  delete document.documentElement.dataset.input;
  vi.unstubAllGlobals();
});

function animationDriver(dialog) {
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
  return motions;
}

it("keeps content and modality until exit finishes, then restores focus", async () => {
  const trigger = document.createElement("button");
  document.body.append(trigger);
  trigger.focus();
  const view = render(
    <Dialog open={false} title="Панель" onClose={() => {}} />,
  );
  const dialog = document.querySelector("dialog");
  const motions = animationDriver(dialog);
  view.rerender(
    <Dialog open title="Панель" onClose={() => {}}>
      <p>Содержимое</p>
    </Dialog>,
  );
  await act(async () => motions[0].finish());
  view.rerender(<Dialog open={false} title="Панель" onClose={() => {}} />);
  expect(dialog.open).toBe(true);
  expect(screen.getByText("Содержимое")).toBeTruthy();
  await act(async () => motions[1].finish());
  expect(dialog.open).toBe(false);
  expect(document.activeElement).toBe(trigger);
  trigger.remove();
});

it("does not let a superseded exit close a reopened dialog", async () => {
  const view = render(
    <StrictMode>
      <Dialog open={false} title="Панель" onClose={() => {}} />
    </StrictMode>,
  );
  const dialog = document.querySelector("dialog");
  const motions = animationDriver(dialog);
  const update = (open) =>
    view.rerender(
      <StrictMode>
        <Dialog open={open} title="Панель" onClose={() => {}} />
      </StrictMode>,
    );
  update(true);
  update(false);
  update(true);
  expect(motions[0].cancel).toHaveBeenCalled();
  expect(motions[1].cancel).toHaveBeenCalled();
  await act(async () => {
    motions[1].finish();
    motions[2].finish();
  });
  expect(dialog.open).toBe(true);
});

it.each(["keyboard", "reduced"])(
  "closes immediately with %s motion preference",
  (mode) => {
    if (mode === "keyboard")
      document.documentElement.dataset.input = "keyboard";
    vi.stubGlobal("matchMedia", () => ({ matches: mode === "reduced" }));
    const view = render(
      <Dialog open={false} title="Панель" onClose={() => {}} />,
    );
    const dialog = document.querySelector("dialog");
    animationDriver(dialog);
    view.rerender(<Dialog open title="Панель" onClose={() => {}} />);
    view.rerender(<Dialog open={false} title="Панель" onClose={() => {}} />);
    expect(dialog.open).toBe(false);
    expect(dialog.animate).not.toHaveBeenCalled();
  },
);

it("retains an inert disclosure during exit and cancels that exit on reversal", async () => {
  const view = render(
    <Reveal open>
      <button>Действие</button>
    </Reveal>,
  );
  const element = view.container.firstChild;
  const motions = animationDriver(element);
  view.rerender(<Reveal open={false} />);
  expect(element.hasAttribute("inert")).toBe(true);
  expect(screen.queryByRole("button", { name: "Действие" })).toBeNull();
  expect(element.textContent).toBe("Действие");
  view.rerender(
    <Reveal open>
      <button>Новый выбор</button>
    </Reveal>,
  );
  expect(motions[0].cancel).toHaveBeenCalled();
  await act(async () => motions[0].finish());
  expect(screen.getByRole("button", { name: "Новый выбор" })).toBeTruthy();
  await act(async () => motions[1].finish());
  view.rerender(<Reveal open={false} />);
  await act(async () => motions[2].finish());
  expect(view.container.childElementCount).toBe(0);
});

it("starts a sheet exit at its dragged position and clears it after closing", async () => {
  const view = render(<Dialog open title="Лист" onClose={() => {}} />);
  const dialog = document.querySelector("dialog");
  const motions = animationDriver(dialog);
  dialog.style.transform = "translateY(120px)";
  view.rerender(<Dialog open={false} title="Лист" onClose={() => {}} />);
  expect(dialog.animate.mock.calls[0][0][0].transform).toBe(
    "translateY(120px)",
  );
  await act(async () => motions[0].finish());
  expect(dialog.open).toBe(false);
  expect(dialog.style.transform).toBe("");
});

it("hands focus between dialogs without an older exit stealing it", async () => {
  function Pair({ first, second }) {
    return (
      <>
        <button>Начало</button>
        <Dialog open={first} title="Первая" onClose={() => {}}>
          <button>Вторая панель</button>
        </Dialog>
        <Dialog open={second} title="Вторая" onClose={() => {}}>
          <input aria-label="Поле второй панели" />
        </Dialog>
      </>
    );
  }
  const view = render(<Pair first={false} second={false} />);
  const trigger = screen.getByRole("button", { name: "Начало" });
  trigger.focus();
  const [first, second] = view.container.querySelectorAll("dialog");
  const firstMotions = animationDriver(first);
  const secondMotions = animationDriver(second);
  view.rerender(<Pair first second={false} />);
  await act(async () => firstMotions[0].finish());
  screen.getByRole("button", { name: "Вторая панель" }).focus();
  view.rerender(<Pair first={false} second />);
  const field = screen.getByRole("textbox", { name: "Поле второй панели" });
  field.focus();
  await act(async () => {
    firstMotions[1].finish();
    secondMotions[0].finish();
  });
  expect(document.activeElement).toBe(field);
  view.rerender(<Pair first={false} second={false} />);
  await act(async () => secondMotions[1].finish());
  expect(document.activeElement).toBe(trigger);
});

it("releases modal children after exit and restores focus on unmount", async () => {
  const trigger = document.createElement("button");
  document.body.append(trigger);
  trigger.focus();
  const view = render(
    <Dialog open title="Панель" onClose={() => {}}>
      <input aria-label="Черновик" />
    </Dialog>,
  );
  const dialog = view.container.querySelector("dialog");
  const motions = animationDriver(dialog);
  screen.getByRole("textbox", { name: "Черновик" }).focus();
  view.rerender(
    <Dialog open={false} title="Панель" onClose={() => {}}>
      <input aria-label="Черновик" />
    </Dialog>,
  );
  await act(async () => motions[0].finish());
  expect(dialog.querySelector("input")).toBeNull();
  trigger.focus();
  view.rerender(
    <Dialog open title="Панель" onClose={() => {}}>
      <input aria-label="Черновик" />
    </Dialog>,
  );
  screen.getByRole("textbox", { name: "Черновик" }).focus();
  view.unmount();
  expect(document.activeElement).toBe(trigger);
  trigger.remove();
});

it("ignores a queued native close event during a later exit", async () => {
  const after = vi.fn();
  const view = render(
    <Dialog open title="Панель" onClose={() => {}} onAfterClose={after} />,
  );
  const dialog = view.container.querySelector("dialog");
  const motions = animationDriver(dialog);
  view.rerender(
    <Dialog
      open={false}
      title="Панель"
      onClose={() => {}}
      onAfterClose={after}
    />,
  );
  fireEvent(dialog, new Event("close"));
  expect(after).not.toHaveBeenCalled();
  await act(async () => motions[0].finish());
  expect(after).toHaveBeenCalledTimes(1);
  fireEvent(dialog, new Event("close"));
  expect(after).toHaveBeenCalledTimes(1);
});

it("releases a nested select's pointer lock when its dialog exits", async () => {
  const trigger = document.createElement("button");
  document.body.append(trigger);
  trigger.focus();
  const contents = (
    <Select aria-label="Порядок">
      <option value="a">Первый</option>
      <option value="b">Второй</option>
    </Select>
  );
  const view = render(
    <Dialog open title="Панель" onClose={() => {}}>
      {contents}
    </Dialog>,
  );
  const dialog = view.container.querySelector("dialog");
  const motions = animationDriver(dialog);
  fireEvent.keyDown(screen.getByRole("combobox"), { key: "ArrowDown" });
  await screen.findByRole("listbox");
  expect(document.body.style.pointerEvents).toBe("none");
  view.rerender(
    <Dialog open={false} title="Панель" onClose={() => {}}>
      {contents}
    </Dialog>,
  );
  await act(async () => motions[0].finish());
  expect(screen.queryByRole("listbox")).toBeNull();
  expect(document.body.style.pointerEvents).toBe("");
  trigger.remove();
});

it("requests dismissal only once while a close transition is running", () => {
  const close = vi.fn();
  function Example() {
    const [open, setOpen] = useState(true);
    return (
      <Dialog
        open={open}
        title="Панель"
        onClose={() => {
          close();
          setOpen(false);
        }}
      />
    );
  }
  const view = render(<Example />);
  animationDriver(view.container.querySelector("dialog"));
  fireEvent.click(screen.getByRole("button", { name: "Закрыть" }));
  fireEvent.click(screen.getByRole("button", { name: "Закрыть" }));
  expect(close).toHaveBeenCalledTimes(1);
});
