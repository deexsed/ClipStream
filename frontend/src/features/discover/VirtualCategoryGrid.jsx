import { useLayoutEffect, useRef, useState } from "react";
import "./VirtualCategoryGrid.css";

const overscan = 2;
const initialWindow = { columns: 4, pitch: 170, gap: 10, first: 0, last: 5 };

export default function VirtualCategoryGrid({
  games,
  viewportSelector,
  className = "",
  renderCard,
  isDisabled,
}) {
  const list = useRef(null);
  const pendingFocus = useRef(null);
  const [focusedIndex, setFocusedIndex] = useState(null);
  const [visible, setVisible] = useState(initialWindow);
  const { columns, pitch, gap, first, last } = visible;
  const totalRows = Math.ceil(games.length / columns);
  const rows = new Set();
  for (let row = first; row < Math.min(last, totalRows); row++) rows.add(row);
  if (focusedIndex !== null && focusedIndex < games.length)
    rows.add(Math.floor(focusedIndex / columns));

  useLayoutEffect(() => {
    const grid = list.current;
    const viewport = grid.closest(viewportSelector) || document.documentElement;
    const scrollTarget =
      viewport === document.documentElement ? window : viewport;
    let frame;
    let geometry;
    const updateWindow = () => {
      frame = undefined;
      const { columns, pitch, gap, top } = geometry;
      const rowCount = Math.ceil(games.length / columns);
      const start = Math.floor((viewport.scrollTop - top) / pitch);
      const end = Math.ceil(
        (viewport.scrollTop + viewport.clientHeight - top) / pitch,
      );
      const first = Math.min(rowCount - 1, Math.max(0, start - overscan));
      const last = Math.min(rowCount, Math.max(first + 1, end + overscan));
      setVisible((before) =>
        before.columns === columns &&
        before.pitch === pitch &&
        before.gap === gap &&
        before.first === first &&
        before.last === last
          ? before
          : { columns, pitch, gap, first, last },
      );
    };
    const measure = () => {
      const row = grid.firstElementChild;
      const style = getComputedStyle(grid);
      const columns =
        Number(style.getPropertyValue("--category-columns")) ||
        initialWindow.columns;
      const gap =
        parseFloat(style.getPropertyValue("--category-gap")) ||
        initialWindow.gap;
      // Read the resolved layout height, independent of the dialog's opening transform.
      const height = row && parseFloat(getComputedStyle(row).height);
      geometry = {
        columns,
        pitch: (height || initialWindow.pitch - gap) + gap,
        gap,
        top:
          grid.getBoundingClientRect().top -
          viewport.getBoundingClientRect().top +
          (scrollTarget === window
            ? 0
            : viewport.scrollTop - viewport.clientTop),
      };
      if (frame !== undefined) cancelAnimationFrame(frame);
      updateWindow();
    };
    const onScroll = () => {
      if (frame === undefined) frame = requestAnimationFrame(updateWindow);
    };
    measure();
    const observer = new ResizeObserver(measure);
    observer.observe(viewport);
    observer.observe(grid);
    scrollTarget.addEventListener("scroll", onScroll, { passive: true });
    return () => {
      observer.disconnect();
      scrollTarget.removeEventListener("scroll", onScroll);
      if (frame !== undefined) cancelAnimationFrame(frame);
    };
  }, [games, viewportSelector]);

  useLayoutEffect(() => {
    if (pendingFocus.current === null) return;
    list.current
      .querySelector(`[data-category-index="${pendingFocus.current}"]`)
      ?.focus();
    pendingFocus.current = null;
  });

  return (
    <div
      ref={list}
      className={`category-virtual-grid ${className}`}
      style={{ height: Math.max(0, totalRows * pitch - gap) }}
      onFocus={(event) => {
        const index = event.target.dataset.categoryIndex;
        if (index !== undefined) setFocusedIndex(Number(index));
      }}
      onBlur={(event) => {
        if (!event.currentTarget.contains(event.relatedTarget))
          setFocusedIndex(null);
      }}
      onKeyDown={(event) => {
        const index = Number(event.target.dataset.categoryIndex);
        if (event.key !== "Tab" || !Number.isInteger(index)) return;
        const direction = event.shiftKey ? -1 : 1;
        for (
          let next = index + direction;
          next >= 0 && next < games.length;
          next += direction
        ) {
          if (isDisabled?.(games[next])) continue;
          if (!list.current.querySelector(`[data-category-index="${next}"]`)) {
            event.preventDefault();
            pendingFocus.current = next;
            setFocusedIndex(next);
          }
          break;
        }
      }}
    >
      {[...rows]
        .sort((a, b) => a - b)
        .map((row) => (
          <div
            key={row}
            className="category-virtual-row"
            style={{ top: row * pitch }}
          >
            {games
              .slice(row * columns, (row + 1) * columns)
              .map((game, index) => renderCard(game, row * columns + index))}
          </div>
        ))}
    </div>
  );
}
