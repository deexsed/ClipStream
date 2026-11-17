import { useLayoutEffect, useRef } from "react";
import { Button } from "./Primitives";
import "./SectionNav.css";

export default function SectionNav({
  items,
  value,
  onChange,
  label,
  rail = false,
}) {
  const ref = useRef(null);
  useLayoutEffect(() => {
    const nav = ref.current;
    const active = nav.querySelector('[aria-current="page"]');
    if (!active || nav.scrollWidth <= nav.clientWidth) return;
    const bounds = nav.getBoundingClientRect(),
      item = active.getBoundingClientRect();
    if (item.left < bounds.left) nav.scrollLeft += item.left - bounds.left;
    else if (item.right > bounds.right)
      nav.scrollLeft += item.right - bounds.right;
  }, [value]);
  return (
    <nav
      ref={ref}
      className={rail ? "console-nav" : "profile-tabs"}
      aria-label={label}
    >
      {items.map(([key, title, Icon]) => (
        <Button
          type="button"
          variant="plain"
          key={key}
          aria-current={value === key ? "page" : undefined}
          onClick={() => onChange(key)}
        >
          {Icon && <Icon size={17} aria-hidden="true" />}
          {title}
        </Button>
      ))}
    </nav>
  );
}
