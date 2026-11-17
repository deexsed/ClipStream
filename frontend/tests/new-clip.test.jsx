import React from "react";
import { act, render, screen } from "@testing-library/react";
import { expect, it, vi } from "vitest";
import NewClipBadge from "../src/features/clips/NewClipBadge";
import FeedComposition from "../src/features/discover/FeedComposition";

it("expires at exactly 24 hours without a reload and parses database dates as UTC", () => {
  vi.useFakeTimers();
  vi.setSystemTime(new Date("2026-11-17T12:00:00Z"));
  render(<NewClipBadge published="2026-11-16 12:00:01" />);
  expect(screen.getByText("Новое")).toBeTruthy();
  act(() => vi.advanceTimersByTime(1000));
  expect(screen.queryByText("Новое")).toBeNull();
});

it.each([undefined, "invalid", "2026-11-18T12:00:00Z", "2026-11-16T12:00:00Z"])(
  "does not label an unknown, future or expired publication as new: %s",
  (published) => {
    vi.useFakeTimers();
    vi.setSystemTime(new Date("2026-11-17T12:00:00Z"));
    render(<NewClipBadge published={published} />);
    expect(screen.queryByText("Новое")).toBeNull();
  },
);

it("cleans up the expiry timer and recomputes when a card changes", () => {
  vi.useFakeTimers();
  vi.setSystemTime(new Date("2026-11-17T12:00:00Z"));
  const { rerender, unmount } = render(
    <NewClipBadge published="2026-11-17T11:00:00Z" />,
  );
  expect(vi.getTimerCount()).toBe(1);
  rerender(<NewClipBadge published="2026-11-15T11:00:00Z" />);
  expect(screen.queryByText("Новое")).toBeNull();
  expect(vi.getTimerCount()).toBe(0);
  unmount();
});

it("graphs actual loaded clips and groups remaining categories without losing counts", () => {
  const clips = Array.from({ length: 8 }, (_, index) => ({
    game_name: `Category ${index}`,
  }));
  render(<FeedComposition clips={[...clips, clips[0]]} />);
  const rows = screen.getAllByRole("listitem", { hidden: true });
  expect(rows).toHaveLength(6);
  expect(
    rows.reduce(
      (sum, row) => sum + Number(row.querySelector("strong").textContent),
      0,
    ),
  ).toBe(9);
  expect(rows[0].textContent).toBe("Category 02");
  expect(rows.at(-1).textContent).toBe("Остальные категории3");
});
