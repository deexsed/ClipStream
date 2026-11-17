import {
  memo,
  useEffect,
  useLayoutEffect,
  useMemo,
  useRef,
  useState,
} from "react";
import { LoadMore } from "../../shared/ui/Primitives";

const minCardWidth = 310;
const columnGap = 20;
const rowGap = 26;
const focusable = "a[href], button:not([disabled])";

function rowAt(offsets, y) {
  let low = 0;
  let high = offsets.length - 1;
  while (low < high) {
    const middle = Math.ceil((low + high) / 2);
    if (offsets[middle] <= y) low = middle;
    else high = middle - 1;
  }
  return low;
}

const ClipRow = memo(function ClipRow({
  clips,
  columns,
  row,
  top,
  renderClip,
}) {
  const start = row * columns;
  return (
    <div className="overview-clip-row" data-clip-row={row} style={{ top }}>
      {clips
        .slice(start, start + columns)
        .map((clip, index) => renderClip(clip, start + index))}
    </div>
  );
});

export default function VirtualClipGrid({ clips, query, renderClip }) {
  const grid = useRef(null);
  const sentinel = useRef(null);
  const pendingFocus = useRef(null);
  const [viewport, setViewport] = useState(null);
  const [windowRect, setWindowRect] = useState({
    width: 0,
    top: 0,
    bottom: 900,
  });
  const [measurements, setMeasurements] = useState(null);
  const [focusedRow, setFocusedRow] = useState(null);
  const [nearEnd, setNearEnd] = useState(false);
  const { width, top, bottom } = windowRect;
  const columns = Math.max(
    1,
    Math.floor((width + columnGap) / (minCardWidth + columnGap)),
  );
  const estimate =
    ((width ? (width - (columns - 1) * columnGap) / columns : minCardWidth) *
      9) /
      16 +
    100;
  const rowCount = Math.ceil(clips.length / columns);
  const offsets = useMemo(() => {
    const positions = [0];
    for (let i = 0; i < rowCount; i++) {
      const height =
        measurements?.width === width ? measurements.heights.get(i) : undefined;
      positions.push(positions[i] + (height ?? estimate) + rowGap);
    }
    return positions;
  }, [rowCount, measurements, width, estimate]);

  useLayoutEffect(() => {
    const node = grid.current;
    const root = node.closest(".main-content");
    setViewport(root);
    const target = root || window;
    let frame;
    const read = () => {
      frame = undefined;
      const rect = node.getBoundingClientRect();
      const viewportTop = root
        ? root.getBoundingClientRect().top + root.clientTop
        : 0;
      const next = {
        width: node.clientWidth,
        top: viewportTop - rect.top,
        bottom:
          viewportTop - rect.top + (root?.clientHeight ?? window.innerHeight),
      };
      setWindowRect((previous) =>
        previous.width === next.width &&
        previous.top === next.top &&
        previous.bottom === next.bottom
          ? previous
          : next,
      );
    };
    const schedule = () => {
      if (frame === undefined) frame = requestAnimationFrame(read);
    };
    read();
    const observer = new ResizeObserver(schedule);
    observer.observe(node);
    observer.observe(node.parentElement);
    if (root) observer.observe(root);
    target.addEventListener("scroll", schedule, { passive: true });
    window.addEventListener("resize", schedule);
    return () => {
      observer.disconnect();
      target.removeEventListener("scroll", schedule);
      window.removeEventListener("resize", schedule);
      if (frame !== undefined) cancelAnimationFrame(frame);
    };
  }, []);

  const first = Math.max(0, rowAt(offsets, top) - 1);
  const last = Math.min(rowCount, rowAt(offsets, bottom) + 2);
  const pinned = focusedRow === null ? null : Math.floor(focusedRow / columns);
  const rows = new Set();
  for (let row = first; row < last; row++) rows.add(row);
  if (pinned !== null && pinned < rowCount) rows.add(pinned);

  useLayoutEffect(() => {
    if (!width) return;
    const observer = new ResizeObserver((entries) => {
      const sizes = entries
        .map(({ target }) => [
          Number(target.dataset.clipRow),
          target.offsetHeight,
        ])
        .filter(([, height]) => height > 0);
      if (!sizes.length) return;
      setMeasurements((previous) => {
        const measured =
          previous?.width === width ? previous.heights : new Map();
        if (sizes.every(([row, height]) => measured.get(row) === height))
          return previous;
        const heights = new Map(measured);
        for (const [row, height] of sizes) heights.set(row, height);
        return { width, heights };
      });
    });
    for (const row of grid.current.querySelectorAll("[data-clip-row]"))
      observer.observe(row);
    return () => observer.disconnect();
  }, [first, last, pinned, width]);

  const autoLoad = typeof IntersectionObserver !== "undefined";
  useEffect(() => {
    if (!autoLoad || !sentinel.current) return;
    const observer = new IntersectionObserver(
      ([entry]) => setNearEnd(entry.isIntersecting),
      { root: viewport, rootMargin: "0px 0px 10px 0px" },
    );
    observer.observe(sentinel.current);
    return () => observer.disconnect();
  }, [autoLoad, viewport, query.hasNextPage]);

  useEffect(() => {
    // Recheck the current geometry. An earlier intersection may still be
    // queued after appending enough rows to move the sentinel out of range.
    if (
      !nearEnd ||
      !query.hasNextPage ||
      query.isFetching ||
      query.isFetchNextPageError ||
      offsets[rowCount - 1] > bottom + 10
    )
      return;
    void query.fetchNextPage({ cancelRefetch: false });
  }, [nearEnd, query, offsets, rowCount, bottom]);

  useLayoutEffect(() => {
    if (!pendingFocus.current) return;
    const { row, backward } = pendingFocus.current;
    const controls = grid.current
      .querySelector(`[data-clip-row="${row}"]`)
      ?.querySelectorAll(focusable);
    if (controls?.length) controls[backward ? controls.length - 1 : 0].focus();
    pendingFocus.current = null;
  });

  return (
    <>
      <div
        className="overview-grid"
        aria-label="Лента клипов"
        ref={grid}
        style={{
          height: Math.max(0, offsets[rowCount] - rowGap),
          "--clip-columns": columns,
        }}
        onFocus={(event) => {
          const row = event.target.closest("[data-clip-row]");
          if (row) setFocusedRow(Number(row.dataset.clipRow) * columns);
        }}
        onBlur={(event) => {
          if (!event.currentTarget.contains(event.relatedTarget))
            setFocusedRow(null);
        }}
        onKeyDown={(event) => {
          if (event.key !== "Tab") return;
          const row = event.target.closest("[data-clip-row]");
          if (!row) return;
          const controls = row.querySelectorAll(focusable);
          const backward = event.shiftKey;
          if (event.target !== controls[backward ? 0 : controls.length - 1])
            return;
          const adjacent = Number(row.dataset.clipRow) + (backward ? -1 : 1);
          if (adjacent < 0 || adjacent >= rowCount || rows.has(adjacent))
            return;
          event.preventDefault();
          pendingFocus.current = { row: adjacent, backward };
          setFocusedRow(adjacent * columns);
        }}
      >
        {[...rows]
          .sort((a, b) => a - b)
          .map((row) => (
            <ClipRow
              key={row}
              row={row}
              top={offsets[row]}
              columns={columns}
              clips={clips}
              renderClip={renderClip}
            />
          ))}
        {autoLoad && query.hasNextPage && (
          <div
            ref={sentinel}
            className="overview-feed-sentinel"
            aria-hidden="true"
            style={{ top: offsets[Math.max(0, rowCount - 1)] }}
          />
        )}
      </div>
      {(!autoLoad || query.isFetchNextPageError) && <LoadMore query={query} />}
      {autoLoad && query.isFetchingNextPage && (
        <div className="loading-state" role="status">
          Загружаем клипы…
        </div>
      )}
    </>
  );
}
