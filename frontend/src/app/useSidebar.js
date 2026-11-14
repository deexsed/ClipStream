import { useEffect, useRef, useState } from "react";

const storageKey = "clipstream.sidebar.compact";

export default function useSidebar(pathname) {
  const [compact, setCompact] = useState(() => {
    try {
      return localStorage.getItem(storageKey) === "true";
    } catch {
      return false;
    }
  });
  const [mobile, setMobile] = useState(
    () => window.matchMedia("(max-width: 1000px)").matches,
  );
  const [open, setOpen] = useState(false);
  const gesture = useRef(null);
  const suppressClick = useRef(0);
  useEffect(() => {
    try {
      localStorage.setItem(storageKey, String(compact));
    } catch {
      // The panel remains usable when browser storage is unavailable.
    }
  }, [compact]);

  useEffect(() => {
    const media = window.matchMedia("(max-width: 1000px)");
    const update = () => {
      setMobile(media.matches);
      setOpen(false);
      gesture.current = null;
    };
    media.addEventListener("change", update);
    update();
    return () => media.removeEventListener("change", update);
  }, []);

  useEffect(() => setOpen(false), [pathname]);

  const expanded = mobile ? open : !compact;
  const setExpanded = (value) => {
    if (mobile) setOpen(value);
    else setCompact(!value);
  };

  const gestures = {
    onTouchStart(event) {
      suppressClick.current = 0;
      gesture.current =
        event.touches.length === 1
          ? {
              x: event.touches[0].clientX,
              y: event.touches[0].clientY,
              started: performance.now(),
            }
          : null;
    },
    onTouchMove(event) {
      const start = gesture.current;
      if (!start) return;
      if (event.touches.length === 1 && !start.horizontal) {
        const dx = Math.abs(event.touches[0].clientX - start.x);
        const dy = Math.abs(event.touches[0].clientY - start.y);
        if (Math.max(dx, dy) >= 10) {
          if (dy >= dx) {
            gesture.current = null;
            return;
          }
          start.horizontal = true;
        }
      }
      if (
        event.touches.length !== 1 ||
        Math.abs(event.touches[0].clientY - start.y) > 24
      ) {
        gesture.current = null;
      }
    },
    onTouchEnd(event) {
      const start = gesture.current;
      gesture.current = null;
      if (!start || event.touches.length || !event.changedTouches.length)
        return;
      const dx = event.changedTouches[0].clientX - start.x;
      const dy = event.changedTouches[0].clientY - start.y;
      if (
        performance.now() - start.started > 700 ||
        Math.abs(dx) < 48 ||
        Math.abs(dx) < Math.abs(dy) * 2
      )
        return;
      suppressClick.current = performance.now() + 400;
      setExpanded(dx > 0);
    },
    onTouchCancel() {
      gesture.current = null;
    },
    onClickCapture(event) {
      if (event.detail !== 0 && performance.now() < suppressClick.current) {
        event.preventDefault();
        event.stopPropagation();
        suppressClick.current = 0;
      }
    },
  };

  return {
    mobile,
    compact,
    open,
    expanded,
    setOpen,
    setExpanded,
    gestures,
  };
}
