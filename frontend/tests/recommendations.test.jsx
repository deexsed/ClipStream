import React, { useRef } from "react";
import {
  act,
  render,
  screen,
  fireEvent,
  waitFor,
} from "@testing-library/react";
import { beforeEach, afterEach, expect, it, vi } from "vitest";
import { QueryClientProvider } from "@tanstack/react-query";
import { queryClient } from "../src/shared/api/queries";
import { useRecommendationFeed } from "../src/shared/api/recommendations";
import { useRecommendationImpressions } from "../src/shared/useRecommendationImpressions";

let observers;
beforeEach(() => {
  observers = [];
  vi.stubGlobal(
    "IntersectionObserver",
    class {
      constructor(callback) {
        this.callback = callback;
        this.elements = [];
        observers.push(this);
      }
      observe(element) {
        this.elements.push(element);
      }
      disconnect() {}
    },
  );
});
afterEach(() => vi.unstubAllGlobals());
function Impression({ token = "a".repeat(32), active = true }) {
  const ref = useRef(null);
  useRecommendationImpressions(ref, token);
  return (
    <article
      ref={ref}
      data-recommendation-delivery={token}
      data-recommendation-active={String(active)}
    >
      clip
    </article>
  );
}
function visible(ratio = 1) {
  for (const o of observers)
    o.callback(
      o.elements.map((target) => ({
        target,
        isIntersecting: ratio > 0,
        intersectionRatio: ratio,
      })),
    );
}
it("records a real visible impression after one continuous second, once", async () => {
  vi.useFakeTimers();
  const fetch = vi
    .spyOn(globalThis, "fetch")
    .mockResolvedValue(new Response("{}"));
  render(<Impression />);
  act(() => visible());
  await act(async () => vi.advanceTimersByTimeAsync(900));
  expect(fetch).not.toHaveBeenCalled();
  act(() => visible(0));
  await act(async () => vi.advanceTimersByTimeAsync(500));
  expect(fetch).not.toHaveBeenCalled();
  act(() => visible());
  await act(async () => vi.advanceTimersByTimeAsync(1200));
  expect(fetch).toHaveBeenCalledTimes(1);
  const body = JSON.parse(fetch.mock.calls[0][1].body);
  expect(body.delivery_ids).toEqual(["a".repeat(32)]);
  act(() => visible(0));
  act(() => visible());
  await act(async () => vi.advanceTimersByTimeAsync(2000));
  expect(fetch).toHaveBeenCalledTimes(1);
});
it("does not credit inactive Rush slides or background tabs", async () => {
  vi.useFakeTimers();
  const fetch = vi
    .spyOn(globalThis, "fetch")
    .mockResolvedValue(new Response("{}"));
  const { rerender } = render(<Impression active={false} />);
  act(() => visible());
  await act(async () => vi.advanceTimersByTimeAsync(1500));
  expect(fetch).not.toHaveBeenCalled();
  Object.defineProperty(document, "visibilityState", {
    configurable: true,
    value: "hidden",
  });
  rerender(<Impression active />);
  act(() => document.dispatchEvent(new Event("visibilitychange")));
  await act(async () => vi.advanceTimersByTimeAsync(1500));
  expect(fetch).not.toHaveBeenCalled();
  Object.defineProperty(document, "visibilityState", {
    configurable: true,
    value: "visible",
  });
  act(() => document.dispatchEvent(new Event("visibilitychange")));
  await act(async () => vi.advanceTimersByTimeAsync(1200));
  expect(fetch).toHaveBeenCalledTimes(1);
});
it("does not count a card covered by a modal and cleans up pending timers", async () => {
  vi.useFakeTimers();
  const fetch = vi
    .spyOn(globalThis, "fetch")
    .mockResolvedValue(new Response("{}"));
  const { unmount } = render(
    <>
      <Impression />
      <dialog open>modal</dialog>
    </>,
  );
  act(() => visible());
  await act(async () => vi.advanceTimersByTimeAsync(1500));
  expect(fetch).not.toHaveBeenCalled();
  unmount();
  await act(async () => vi.advanceTimersByTimeAsync(1500));
  expect(fetch).not.toHaveBeenCalled();
});
function Feed({ viewer = 1, surface = "rush" }) {
  const feed = useRecommendationFeed({ viewer, languages: "ru", surface });
  return (
    <>
      <div>
        {feed.data?.pages
          .flatMap((p) => p.clips)
          .map((c) => (
            <span key={c.id}>{c.title}</span>
          ))}
      </div>
      <button onClick={() => feed.fetchNextPage()}>next</button>
      <button onClick={feed.startNewSession}>refresh</button>
    </>
  );
}
it("uses opaque cursors and isolates Rush, home, and account caches", async () => {
  // HTTP LAN origins expose getRandomValues, but not randomUUID.
  vi.stubGlobal("crypto", {
    getRandomValues: globalThis.crypto.getRandomValues.bind(globalThis.crypto),
  });
  const fetch = vi
    .spyOn(globalThis, "fetch")
    .mockImplementation(async (path) => {
      const url = new URL(path, "http://localhost");
      const next = url.searchParams.get("cursor");
      return new Response(
        JSON.stringify({
          items: [
            { clip: { id: next ? 2 : 1, title: next ? "second" : "first" } },
          ],
          next_cursor: next ? null : "opaque:24",
        }),
      );
    });
  const { rerender } = render(
    <QueryClientProvider client={queryClient}>
      <Feed />
    </QueryClientProvider>,
  );
  await screen.findByText("first");
  fireEvent.click(screen.getByText("next"));
  await screen.findByText("second");
  expect(
    fetch.mock.calls.some(
      ([path]) =>
        path.includes("cursor=opaque%3A24") && path.includes("surface=rush"),
    ),
  ).toBe(true);
  expect(fetch.mock.calls.every(([path]) => !path.includes("exclude="))).toBe(
    true,
  );
  rerender(
    <QueryClientProvider client={queryClient}>
      <Feed viewer={2} surface="home" />
    </QueryClientProvider>,
  );
  await waitFor(() =>
    expect(
      fetch.mock.calls.some(([path]) => path.includes("surface=home")),
    ).toBe(true),
  );
  expect(
    queryClient
      .getQueryCache()
      .findAll({ queryKey: ["recommendations"] })
      .some((q) => q.queryKey[1] === 2 && q.queryKey[3] === "home"),
  ).toBe(true);
  expect(screen.queryByText("second")).toBeNull();
});

it("starts an explicit new queue after exhausting a selection", async () => {
  const sessions = [];
  vi.spyOn(globalThis, "fetch").mockImplementation(async (path) => {
    const session = new URL(path, "http://localhost").searchParams.get(
      "session_id",
    );
    sessions.push(session);
    return new Response(
      JSON.stringify({
        items: [
          {
            clip: {
              id: sessions.length,
              title: `selection ${sessions.length}`,
            },
          },
        ],
        next_cursor: null,
      }),
    );
  });
  render(
    <QueryClientProvider client={queryClient}>
      <Feed />
    </QueryClientProvider>,
  );
  await screen.findByText("selection 1");
  fireEvent.click(screen.getByText("refresh"));
  await screen.findByText("selection 2");
  expect(sessions[0]).not.toBe(sessions[1]);
  expect(screen.queryByText("selection 1")).toBeNull();
});
