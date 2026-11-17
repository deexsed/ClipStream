import React from "react";
import { act, renderHook, waitFor } from "@testing-library/react";
import { expect, it, vi } from "vitest";
import {
  focusManager,
  onlineManager,
  QueryClientProvider,
} from "@tanstack/react-query";
import { pageItems, queryClient, usePages } from "../src/shared/api/queries";
import { useRecommendationFeed } from "../src/shared/api/recommendations";

const wrapper = ({ children }) => (
  <QueryClientProvider client={queryClient}>{children}</QueryClientProvider>
);
const json = (data, status = 200) =>
  new Response(JSON.stringify(data), { status });

it("creates a new home session after a document reload, while sharing the current document's cache", async () => {
  const sessions = [];
  vi.spyOn(globalThis, "fetch").mockImplementation(async (path) => {
    sessions.push(
      new URL(path, "http://localhost").searchParams.get("session_id"),
    );
    return json({ items: [{ clip: { id: sessions.length } }] });
  });
  const first = renderHook(
    () => useRecommendationFeed({ viewer: 1, languages: "ru" }),
    { wrapper },
  );
  await waitFor(() => expect(first.result.current.isSuccess).toBe(true));
  const sameDocument = renderHook(
    () => useRecommendationFeed({ viewer: 1, languages: "ru" }),
    { wrapper },
  );
  expect(sessions).toHaveLength(1);
  sameDocument.unmount();
  first.unmount();
  // A full document reload recreates the QueryClient as well as the hook.
  queryClient.clear();
  const reloaded = renderHook(
    () => useRecommendationFeed({ viewer: 1, languages: "ru" }),
    { wrapper },
  );
  await waitFor(() => expect(reloaded.result.current.isSuccess).toBe(true));
  expect(sessions).toHaveLength(2);
  expect(sessions[0]).not.toBe(sessions[1]);
});

for (const recommendations of [false, true]) {
  it(`preserves ${recommendations ? "recommended" : "browse"} clips on remount and reconnect, appends on demand, and allows explicit refresh`, async () => {
    let imported = false;
    const fetch = vi
      .spyOn(globalThis, "fetch")
      .mockImplementation(async (path) => {
        const params = new URL(path, "http://localhost").searchParams;
        const next = recommendations
          ? params.get("cursor")
          : Number(params.get("offset")) > 0;
        const clip = {
          id: next ? 2 : imported ? 3 : 1,
          title: next ? "next" : imported ? "imported" : "original",
        };
        return json(
          recommendations
            ? { items: [{ clip }], next_cursor: next ? null : "opaque:1" }
            : { clips: [clip], has_more: !next },
        );
      });
    const useFeed = () =>
      recommendations
        ? useRecommendationFeed({ viewer: 1, languages: "ru", pageSize: 1 })
        : usePages("/clips", "clips", { sort: "recent" }, { pageSize: 1 });
    const first = renderHook(useFeed, { wrapper });
    await waitFor(() => expect(first.result.current.isSuccess).toBe(true));
    imported = true;
    // Simulate a stale cache so the checks exercise each automatic trigger.
    await act(() => queryClient.invalidateQueries({ refetchType: "none" }));
    const second = renderHook(useFeed, { wrapper });
    try {
      await act(async () => {
        focusManager.setFocused(false);
        onlineManager.setOnline(false);
        focusManager.setFocused(true);
        onlineManager.setOnline(true);
        await new Promise((resolve) => setTimeout(resolve, 30));
      });
      expect(fetch).toHaveBeenCalledTimes(1);
      expect(
        pageItems(second.result.current, "clips").map((clip) => clip.id),
      ).toEqual([1]);

      await act(() => second.result.current.fetchNextPage());
      await waitFor(() =>
        expect(
          pageItems(second.result.current, "clips").map((clip) => clip.id),
        ).toEqual([1, 2]),
      );
      expect(fetch).toHaveBeenCalledTimes(2);

      await act(() => second.result.current.refetch());
      await waitFor(() =>
        expect(
          pageItems(second.result.current, "clips").map((clip) => clip.id),
        ).toEqual([3, 2]),
      );
    } finally {
      focusManager.setFocused(undefined);
      onlineManager.setOnline(true);
    }
  });
}

it("renews an expired cursor, skips duplicate-only pages and appends new clips", async () => {
  let firstSession;
  const original = { id: 1, title: "original", delivery_id: "original-token" };
  const fetch = vi
    .spyOn(globalThis, "fetch")
    .mockImplementation(async (path) => {
      const params = new URL(path, "http://localhost").searchParams;
      const cursor = params.get("cursor");
      const session = params.get("session_id");
      firstSession ??= session;
      if (cursor === "expired:1")
        return json({ error: "Recommendation cursor expired" }, 400);
      if (!cursor)
        return json({
          items: [
            {
              clip:
                session === firstSession
                  ? original
                  : { ...original, title: "changed" },
            },
          ],
          next_cursor: session === firstSession ? "expired:1" : "renewed:1",
        });
      if (cursor === "renewed:1")
        return json({
          items: [
            { clip: original },
            { clip: { id: 2, title: "new" } },
            { clip: { id: 2, title: "duplicate" } },
          ],
          next_cursor: "renewed:2",
        });
      return json({
        items: [{ clip: { id: 3, title: "third" } }],
        next_cursor: null,
      });
    });
  const { result } = renderHook(
    () => useRecommendationFeed({ viewer: 1, languages: "ru" }),
    { wrapper },
  );
  await waitFor(() => expect(result.current.isSuccess).toBe(true));
  const firstPage = result.current.data.pages[0];
  await act(() => result.current.fetchNextPage());
  await waitFor(() => expect(result.current.data.pages).toHaveLength(2));
  expect(result.current.data.pages[0]).toBe(firstPage);
  expect(pageItems(result.current, "clips")).toEqual([
    original,
    { id: 2, title: "new" },
  ]);
  expect(result.current.data.pages[1].clips).toHaveLength(1);
  expect(result.current.data.pages[1].items).toHaveLength(1);
  expect(fetch).toHaveBeenCalledTimes(4);
  const renewed = new URL(fetch.mock.calls[2][0], "http://localhost")
    .searchParams;
  expect(renewed.get("session_id")).not.toBe(firstSession);
  expect(renewed.get("cursor")).toBe("");
  await act(() => result.current.fetchNextPage());
  expect(result.current.error).toBeNull();
  await waitFor(() => expect(result.current.hasNextPage).toBe(false));
  expect(pageItems(result.current, "clips").map((clip) => clip.id)).toEqual([
    1, 2, 3,
  ]);
});

it.each([
  "Recommendation cursor scope mismatch",
  "Invalid recommendation cursor",
])(
  "preserves the list and reports %s without renewing the session",
  async (error) => {
    const fetch = vi
      .spyOn(globalThis, "fetch")
      .mockImplementation(async (path) =>
        new URL(path, "http://localhost").searchParams.get("cursor")
          ? json({ error }, 400)
          : json({ items: [{ clip: { id: 1 } }], next_cursor: "invalid:1" }),
      );
    const { result } = renderHook(
      () => useRecommendationFeed({ viewer: 1, languages: "ru" }),
      { wrapper },
    );
    await waitFor(() => expect(result.current.isSuccess).toBe(true));
    await act(() => result.current.fetchNextPage());
    await waitFor(() => expect(result.current.isFetchNextPageError).toBe(true));
    expect(pageItems(result.current, "clips")).toEqual([{ id: 1 }]);
    expect(fetch).toHaveBeenCalledTimes(2);
  },
);

it("finishes a renewed queue containing only duplicates without adding repeats", async () => {
  const fetch = vi
    .spyOn(globalThis, "fetch")
    .mockResolvedValueOnce(
      json({ items: [{ clip: { id: 1 } }], next_cursor: "expired:1" }),
    )
    .mockResolvedValueOnce(
      json({ error: "Recommendation cursor expired" }, 400),
    )
    .mockResolvedValueOnce(
      json({ items: [{ clip: { id: 1 } }], next_cursor: null }),
    );
  const { result } = renderHook(
    () => useRecommendationFeed({ viewer: 1, languages: "ru" }),
    { wrapper },
  );
  await waitFor(() => expect(result.current.isSuccess).toBe(true));
  expect(result.current.hasNextPage).toBe(true);
  await act(() => result.current.fetchNextPage());
  await waitFor(() => {
    expect(result.current.error).toBeNull();
    expect(result.current.hasNextPage).toBe(false);
  });
  expect(result.current.data.pages[1].clips).toEqual([]);
  expect(pageItems(result.current, "clips")).toEqual([{ id: 1 }]);
  expect(fetch).toHaveBeenCalledTimes(3);
});
