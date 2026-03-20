import { useState, useEffect, useRef } from 'react';
import { streams } from '../api/client';

const globalCache = {};
const CACHE_TTL = 60_000;

export default function useLiveStatus(clips) {
  const [liveMap, setLiveMap] = useState({});
  const lastKey = useRef('');

  useEffect(() => {
    if (!clips || clips.length === 0) return;

    const names = [...new Set(clips.map((c) => c.broadcaster_name).filter(Boolean))];
    if (names.length === 0) return;

    const key = names.sort().join(',');
    if (key === lastKey.current) return;
    lastKey.current = key;

    const now = Date.now();
    const uncached = names.filter((n) => !globalCache[n] || now - globalCache[n].ts > CACHE_TTL);

    if (uncached.length === 0) {
      const map = {};
      names.forEach((n) => { map[n] = globalCache[n]?.live || false; });
      setLiveMap(map);
      return;
    }

    streams.isLive(names)
      .then((data) => {
        const live = data.live || {};
        const map = {};
        names.forEach((n) => {
          const isLive = !!live[n];
          globalCache[n] = { live: isLive, ts: Date.now() };
          map[n] = isLive;
        });
        setLiveMap(map);
      })
      .catch(() => {});
  }, [clips]);

  return liveMap;
}
