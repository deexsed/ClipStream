import { useLayoutEffect, useRef, useState } from "react";
import "./DataTable.css";

// Keep native table semantics and domain-owned rows; share scrolling, density,
// keyboard access and the visual contract across all consumers.
export default function DataTable({
  label,
  compact = false,
  minWidth,
  className = "",
  children,
  ...props
}) {
  const ref = useRef(null);
  const [scrollable, setScrollable] = useState(false);
  useLayoutEffect(() => {
    const element = ref.current;
    const measure = () =>
      setScrollable(element.scrollWidth > element.clientWidth + 1);
    measure();
    const observer = new ResizeObserver(measure);
    observer.observe(element);
    observer.observe(element.firstElementChild);
    return () => observer.disconnect();
  }, [children]);
  return (
    <div
      ref={ref}
      className="table-scroll"
      tabIndex={scrollable ? 0 : undefined}
      role={scrollable ? "region" : undefined}
      aria-label={scrollable ? label : undefined}
    >
      <table
        {...props}
        aria-label={label}
        className={`data-table ${compact ? "data-table-compact" : ""} ${className}`}
        style={{
          ...props.style,
          ...(minWidth !== undefined ? { minWidth } : {}),
        }}
      >
        {children}
      </table>
    </div>
  );
}
