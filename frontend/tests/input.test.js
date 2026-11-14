import { expect, it } from "vitest";
import { isKeyboardNavigation, isSearchShortcut } from "../src/shared/input";

it("does not switch to keyboard focus rings while editing a pointer-focused field", () => {
  const input = document.createElement("input");
  for (const key of ["a", " ", "ArrowLeft", "Enter"])
    expect(isKeyboardNavigation({ key, target: input })).toBe(false);
  expect(isKeyboardNavigation({ key: "Tab", target: input })).toBe(true);
  expect(isKeyboardNavigation({ key: "Escape", target: input })).toBe(true);
  const button = document.createElement("button");
  expect(isKeyboardNavigation({ key: "Enter", target: button })).toBe(true);
  expect(isKeyboardNavigation({ key: "ArrowDown", target: button })).toBe(true);
});

it("uses the platform modifier and physical KeyK independently of keyboard layout", () => {
  const key = { key: "л", code: "KeyK", ctrlKey: true };
  expect(isSearchShortcut(key, "Win32")).toBe(true);
  expect(isSearchShortcut(key, "Linux x86_64")).toBe(true);
  expect(isSearchShortcut(key, "MacIntel")).toBe(false);
  expect(
    isSearchShortcut({ ...key, ctrlKey: false, metaKey: true }, "MacIntel"),
  ).toBe(true);
  for (const property of [
    "altKey",
    "shiftKey",
    "isComposing",
    "defaultPrevented",
    "repeat",
  ])
    expect(isSearchShortcut({ ...key, [property]: true }, "Windows")).toBe(
      false,
    );
});
