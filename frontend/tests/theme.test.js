import { describe, expect, it } from "vitest";
import { readFileSync } from "node:fs";
import { resolve } from "node:path";

const styles = readFileSync(resolve("src/styles/themes.css"), "utf8");

const light = styles.match(/:root\[data-theme="light"\]\s*\{([^}]+)\}/)[1];
const palette = Object.fromEntries(
  [...light.matchAll(/(--[\w-]+):\s*(#[\da-f]{6});/g)].map(
    ([, name, color]) => [name, color],
  ),
);

function luminance(hex) {
  return hex
    .slice(1)
    .match(/../g)
    .map((channel) => {
      const value = parseInt(channel, 16) / 255;
      return value <= 0.04045
        ? value / 12.92
        : ((value + 0.055) / 1.055) ** 2.4;
    })
    .reduce(
      (sum, value, index) => sum + value * [0.2126, 0.7152, 0.0722][index],
      0,
    );
}
function contrast(first, second) {
  const values = [luminance(palette[first]), luminance(palette[second])].sort(
    (a, b) => b - a,
  );
  return (values[0] + 0.05) / (values[1] + 0.05);
}

describe("light theme contrast", () => {
  it.each(["--bg", "--panel", "--raised", "--theme-selected-bg"])(
    "keeps small text readable on %s",
    (background) => {
      for (const foreground of [
        "--theme-text",
        "--muted",
        "--accent",
        "--theme-selected-text",
      ])
        expect(contrast(foreground, background)).toBeGreaterThanOrEqual(4.5);
    },
  );
  it.each(["--panel", "--raised", "--theme-selected-bg"])(
    "keeps input boundaries visible on %s",
    (background) => {
      expect(
        contrast("--theme-control-line", background),
      ).toBeGreaterThanOrEqual(3);
    },
  );
  it("keeps status text and the live indicator readable", () => {
    expect(
      contrast("--theme-danger-text", "--theme-danger-bg"),
    ).toBeGreaterThanOrEqual(4.5);
    expect(contrast("--theme-success-text", "--panel")).toBeGreaterThanOrEqual(
      4.5,
    );
    expect(contrast("--theme-marker", "--panel")).toBeGreaterThanOrEqual(3);
    expect(contrast("--theme-marker", "--bg")).toBeGreaterThanOrEqual(3);
  });
});
