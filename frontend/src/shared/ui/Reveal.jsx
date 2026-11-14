import { forwardRef, useLayoutEffect, useRef, useState } from "react";

export function motionSettings(element, entering, kind = "panel") {
  const style = getComputedStyle(element);
  const duration = parseFloat(
    style.getPropertyValue(`--motion-${kind}-${entering ? "enter" : "exit"}`),
  );
  return {
    instant:
      document.documentElement.dataset.input === "keyboard" ||
      window.matchMedia?.("(prefers-reduced-motion: reduce)").matches ||
      !element.animate,
    duration: Number.isFinite(duration) ? duration : 0,
    easing: style.getPropertyValue("--ease").trim() || "ease-out",
  };
}

// Preserve a dismissing surface until its exit finishes. Inline disclosures
// animate height because the surrounding content must follow their size.
export default forwardRef(function Reveal(
  {
    open,
    children,
    as: Tag = "div",
    motion = "collapse",
    className = "",
    ...props
  },
  forwardedRef,
) {
  const [present, setPresent] = useState(open);
  const ref = useRef(null);
  const animation = useRef(null);
  const retained = useRef(children);
  if (open) retained.current = children;

  useLayoutEffect(() => {
    const element = ref.current;
    if (!element) return;
    const interrupted = animation.current;
    const current = getComputedStyle(element);
    const from = interrupted && {
      opacity: current.opacity,
      ...(motion === "collapse"
        ? { height: `${element.getBoundingClientRect().height}px` }
        : { transform: current.transform }),
    };
    interrupted?.cancel();
    animation.current = null;
    if (open) setPresent(true);
    element.dataset.motionState = open ? "entering" : "exiting";
    const settings = motionSettings(
      element,
      open,
      motion === "collapse" ? "disclosure" : "panel",
    );
    if (settings.instant) {
      if (open) element.dataset.motionState = "open";
      if (!open) setPresent(false);
      return;
    }
    const hidden =
      motion === "collapse"
        ? { opacity: 0, height: "0px" }
        : { opacity: 0, transform: "translateX(24px)" };
    const shown =
      motion === "collapse"
        ? { opacity: 1, height: `${element.scrollHeight}px` }
        : { opacity: 1, transform: "none" };
    const next = element.animate(
      [from || (open ? hidden : shown), open ? shown : hidden],
      settings,
    );
    animation.current = next;
    next.finished.then(
      () => {
        if (animation.current !== next) return;
        animation.current = null;
        if (!open) setPresent(false);
        else element.dataset.motionState = "open";
      },
      () => {},
    );
  }, [open, motion]);
  useLayoutEffect(
    () => () => {
      const current = animation.current;
      animation.current = null;
      current?.cancel();
    },
    [],
  );

  if (!open && !present) return null;
  return (
    <Tag
      {...props}
      ref={(element) => {
        ref.current = element;
        if (typeof forwardedRef === "function") forwardedRef(element);
        else if (forwardedRef) forwardedRef.current = element;
      }}
      className={`motion-reveal ${className}`}
      inert={!open ? "" : undefined}
      aria-hidden={!open || undefined}
    >
      {open ? children : retained.current}
    </Tag>
  );
});
