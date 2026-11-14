export const number = (value) =>
  new Intl.NumberFormat("ru", {
    notation: "compact",
    maximumFractionDigits: 1,
  }).format(Number(value) || 0);
export const duration = (value) =>
  `${Math.floor(Math.max(0, Number(value) || 0) / 60)}:${String(Math.floor(Math.max(0, Number(value) || 0) % 60)).padStart(2, "0")}`;
export function twitchProfilePath(id, login) {
  if (/^\d+$/.test(String(id || ""))) return `/p/twitch/${id}`;
  return login ? `/channel/${encodeURIComponent(login)}` : null;
}
export function date(value, includeTime = false) {
  if (!value) return "";
  const normalized = /^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}$/.test(value)
    ? value.replace(" ", "T") + "Z"
    : value;
  const parsed = new Date(normalized);
  return Number.isNaN(parsed.getTime())
    ? ""
    : new Intl.DateTimeFormat("ru", {
        day: "numeric",
        month: "short",
        year: "numeric",
        ...(includeTime ? { hour: "2-digit", minute: "2-digit" } : {}),
      }).format(parsed);
}

export function relativeTime(value) {
  if (!value) return "";
  const parsed = new Date(
    /^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}$/.test(value)
      ? value.replace(" ", "T") + "Z"
      : value,
  );
  const seconds = (parsed.getTime() - Date.now()) / 1000;
  if (!Number.isFinite(seconds)) return "";
  if (Math.abs(seconds) < 60) return "только что";
  const format = new Intl.RelativeTimeFormat("ru", { numeric: "auto" });
  if (Math.abs(seconds) < 3600)
    return format.format(Math.round(seconds / 60), "minute");
  if (Math.abs(seconds) < 86400)
    return format.format(Math.round(seconds / 3600), "hour");
  if (Math.abs(seconds) < 604800)
    return format.format(Math.round(seconds / 86400), "day");
  return date(value);
}
