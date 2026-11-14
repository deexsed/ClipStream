import { QueryClient, useInfiniteQuery, useQuery } from "@tanstack/react-query";
import { request, queryString } from "./http";

export const queryClient = new QueryClient({
  defaultOptions: {
    queries: {
      staleTime: 30000,
      gcTime: 300000,
      refetchOnWindowFocus: false,
      retry: (count, error) =>
        count < 1 && (error.status === 0 || error.status >= 500),
      retryDelay: (attempt, error) =>
        Math.min(30000, Math.max(error.retryAfter * 1000, 1000 * 2 ** attempt)),
    },
    mutations: { retry: false },
  },
});

export function useApi(path, options = {}) {
  return useQuery({
    queryKey: ["api", path],
    queryFn: ({ signal }) => request(path, { signal }),
    enabled: Boolean(path),
    ...options,
  });
}

export function usePages(path, field, parameters = {}, options = {}) {
  const size = options.pageSize || 24;
  const signature = queryString({ ...parameters, limit: size });
  return useInfiniteQuery({
    queryKey: ["pages", path, field, signature],
    initialPageParam: 0,
    queryFn: async ({ signal, pageParam }) => {
      const data = await request(`${path}?${signature}&offset=${pageParam}`, {
        signal,
      });
      return { ...data, offset: pageParam };
    },
    getNextPageParam: (last) =>
      last.has_more === false || (last[field] || []).length < size
        ? undefined
        : last.offset + (last[field] || []).length,
    enabled: Boolean(path),
    // Keep displayed clips in place; scrolling appends to this snapshot.
    ...(field === "clips" && {
      staleTime: Infinity,
      refetchOnMount: false,
      refetchOnWindowFocus: false,
      refetchOnReconnect: false,
    }),
    ...options,
  });
}

export function pageItems(result, field, key = "id") {
  const seen = new Set();
  return (result.data?.pages || [])
    .flatMap((page) => page[field] || [])
    .filter((item) => {
      if (seen.has(item[key])) return false;
      seen.add(item[key]);
      return true;
    });
}
