const prefix = "clipstream.category-history.v1.";
const validId = (id) => /^[1-9]\d{0,19}$/.test(String(id));
const count = (value) =>
  Number.isSafeInteger(value) && value >= 0 ? Math.min(value, 1000000) : 0;

export function readCategoryHistory(viewer) {
  try {
    const value = JSON.parse(
      localStorage.getItem(prefix + (viewer || "guest")) || "[]",
    );
    if (!Array.isArray(value)) return [];
    return value
      .slice(0, 64)
      .filter((row) => row && validId(row.id))
      .map((row) => ({
        id: String(row.id),
        selections: count(row.selections),
        views: count(row.views),
        lastUsed: countTimestamp(row.lastUsed),
      }));
  } catch {
    return [];
  }
}

function countTimestamp(value) {
  return Number.isSafeInteger(value) && value > 0
    ? Math.min(value, Date.now())
    : 0;
}

export function recordCategoryUse(id, viewer, kind = "selections") {
  if (!validId(id) || !["selections", "views"].includes(kind)) return;
  const rows = readCategoryHistory(viewer);
  const row = rows.find((row) => row.id === String(id)) || {
    id: String(id),
    selections: 0,
    views: 0,
    lastUsed: 0,
  };
  row[kind] = Math.min(row[kind] + 1, 1000000);
  row.lastUsed = Date.now();
  try {
    localStorage.setItem(
      prefix + (viewer || "guest"),
      JSON.stringify(
        [row, ...rows.filter((item) => item.id !== row.id)].slice(0, 64),
      ),
    );
  } catch {
    /* Category selection remains available without storage. */
  }
}

export function frequentCategories(games, viewed, viewer) {
  const local = new Map(
    readCategoryHistory(viewer).map((row) => [row.id, row]),
  );
  const server = new Map(
    viewed.map((row) => [String(row.id), row.watched_count || 1]),
  );
  return games
    .map((game) => {
      const row = local.get(String(game.id));
      return {
        game,
        score:
          (row?.selections || 0) +
          Math.max(row?.views || 0, server.get(String(game.id)) || 0),
        lastUsed: row?.lastUsed || 0,
      };
    })
    .filter((row) => row.score > 0)
    .sort((a, b) => b.score - a.score || b.lastUsed - a.lastUsed)
    .slice(0, 8)
    .map((row) => row.game);
}
