import {
  useIsMutating,
  useMutation,
  useQueryClient,
} from "@tanstack/react-query";
import { request } from "../../shared/api/http";
import { useSession } from "../auth/Session";

function hasClip(data, id) {
  if (!data || typeof data !== "object") return false;
  if (Array.isArray(data)) return data.some((item) => hasClip(item, id));
  return (
    (String(data.id) === String(id) && "like_count" in data) ||
    ["pages", "clips", "items", "clip"].some((key) => hasClip(data[key], id))
  );
}

function updateClip(data, id, vote) {
  if (!data || typeof data !== "object") return data;
  if (Array.isArray(data)) {
    const next = data.map((item) => updateClip(item, id, vote));
    return next.some((item, index) => item !== data[index]) ? next : data;
  }
  if (String(data.id) === String(id) && "like_count" in data) {
    return {
      ...data,
      like_count: vote.like_count,
      dislike_count: vote.dislike_count,
      user_vote: vote.user_vote,
    };
  }
  let next = data;
  for (const key of ["pages", "clips", "items", "clip"]) {
    if (!(key in data)) continue;
    const value = updateClip(data[key], id, vote);
    if (value !== data[key]) next = { ...next, [key]: value };
  }
  return next;
}

export default function useClipVote(id) {
  const client = useQueryClient();
  const session = useSession();
  const mutationKey = ["clip-vote", session?.user?.id, String(id)];
  const pending = useIsMutating({ mutationKey }) > 0;
  const mutation = useMutation({
    mutationKey,
    mutationFn: (isLike) =>
      request(`/clips/${id}/like`, {
        method: "POST",
        body: { is_like: isLike },
      }),
    onMutate: () =>
      client.cancelQueries({
        predicate: (query) =>
          ["api", "pages", "recommendations"].includes(query.queryKey[0]) &&
          hasClip(query.state.data, id),
      }),
    onSuccess: (value) => {
      client.setQueriesData(
        {
          predicate: (query) =>
            ["api", "pages", "recommendations"].includes(query.queryKey[0]),
        },
        (data) => updateClip(data, id, value),
      );
    },
  });
  return { ...mutation, isPending: pending || mutation.isPending };
}
