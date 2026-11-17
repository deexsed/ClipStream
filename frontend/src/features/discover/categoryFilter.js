export const categoryLimit = 20;

export function categoryIdsFrom(params) {
  const raw = params.get("game_ids") ?? params.get("game_id") ?? "";
  return [
    ...new Set(
      raw
        .split(",")
        .filter(
          (id) =>
            /^[1-9]\d{0,19}$/.test(id) &&
            (id.length < 20 || id <= "18446744073709551615"),
        ),
    ),
  ]
    .slice(0, categoryLimit)
    .sort();
}

export function categoryParams(ids) {
  return ids.length > 1 ? { game_ids: ids.join(",") } : { game_id: ids[0] };
}
