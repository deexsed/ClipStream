import { useEffect, useState } from "react";

// Twitch's publication time is authoritative; created_at is our import time.
export function clipPublicationTime(value) {
  if (!value) return NaN;
  return Date.parse(
    /^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}$/.test(value)
      ? value.replace(" ", "T") + "Z"
      : value,
  );
}

export default function NewClipBadge({ published }) {
  const [now, setNow] = useState(Date.now);
  const started = clipPublicationTime(published);
  const expires = started + 86400000;
  useEffect(() => {
    setNow(Date.now());
    if (!Number.isFinite(started)) return;
    const remaining = expires - Date.now();
    if (remaining <= 0 || started > Date.now()) return;
    const timer = setTimeout(() => setNow(Date.now()), remaining);
    return () => clearTimeout(timer);
  }, [started, expires]);
  return started <= now && now < expires ? (
    <span
      className="clip-new-badge"
      title="Опубликовано на Twitch менее 24 часов назад"
    >
      Новое
    </span>
  ) : null;
}
