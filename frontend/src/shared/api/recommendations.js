import { useMemo } from "react";
import { useInfiniteQuery, useQueryClient } from "@tanstack/react-query";
import { queryString, request } from "./http";
import { randomId } from "../randomId";

export function useRecommendationFeed({
  viewer,
  languages,
  surface = "home",
  pageSize = 24,
  enabled = true,
}) {
  const client = useQueryClient();
  const session = useMemo(
    () => ({ id: randomId() }),
    [viewer, languages, surface],
  );
  const queryKey = ["recommendations", viewer || "guest", languages, surface];
  const query = useInfiniteQuery({
    queryKey,
    initialPageParam: "",
    enabled,
    staleTime: Infinity,
    refetchOnMount: false,
    refetchOnWindowFocus: false,
    refetchOnReconnect: false,
    queryFn: async ({ pageParam, signal }) => {
      const cached = client.getQueryData(queryKey);
      const pageIndex = cached?.pageParams.indexOf(pageParam) ?? -1;
      // During explicit refetches, exclude only preceding pages, not this page.
      const preceding = !pageParam
        ? []
        : pageIndex < 0
          ? cached?.pages || []
          : cached.pages.slice(0, pageIndex);
      const seen = new Set(
        preceding.flatMap((page) => page.clips.map((clip) => String(clip.id))),
      );
      let cursor = pageParam;
      let sessionId = session.id;
      let renewed = false;
      const visited = new Set();
      for (;;) {
        let data;
        try {
          data = await request(
            `/clips/home-feed?${queryString({
              limit: pageSize,
              session_id: sessionId,
              cursor,
              clip_langs: languages,
              surface,
            })}`,
            { signal },
          );
        } catch (error) {
          if (
            !renewed &&
            cursor &&
            error.status === 400 &&
            error.message === "Recommendation cursor expired"
          ) {
            sessionId = randomId();
            cursor = "";
            renewed = true;
            continue;
          }
          throw error;
        }
        const items = (data.items || []).filter((row) => {
          if (!row.clip || seen.has(String(row.clip.id))) return false;
          seen.add(String(row.clip.id));
          return true;
        });
        // A new queue can start with already displayed clips. Skip those pages
        // within this load so a duplicate-only page does not stall scrolling.
        if (!items.length && data.next_cursor) {
          if (visited.has(data.next_cursor) || visited.size >= 200)
            throw new Error("Сервер вернул повторяющийся cursor рекомендаций.");
          visited.add(data.next_cursor);
          cursor = data.next_cursor;
          continue;
        }
        return { ...data, items, clips: items.map((row) => row.clip) };
      }
    },
    getNextPageParam: (last) => last.next_cursor || undefined,
  });
  return {
    ...query,
    startNewSession: () => {
      session.id = randomId();
      return client.resetQueries({ queryKey, exact: true });
    },
  };
}
