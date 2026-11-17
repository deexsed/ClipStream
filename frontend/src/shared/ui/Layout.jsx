import { forwardRef } from "react";
import "./Layout.css";

export const Surface = forwardRef(function Surface(
  { as: Tag = "div", className = "", ...props },
  ref,
) {
  return <Tag {...props} ref={ref} className={`surface ${className}`} />;
});
export function SectionHeading({ className = "", ...props }) {
  return <div {...props} className={`section-label ${className}`} />;
}
