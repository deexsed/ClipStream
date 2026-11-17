import { forwardRef } from "react";

export default forwardRef(function ExternalLink({ rel = "", ...props }, ref) {
  const relationship = [
    ...new Set(["noopener", "noreferrer", ...rel.split(/\s+/).filter(Boolean)]),
  ].join(" ");
  return <a {...props} ref={ref} target="_blank" rel={relationship} />;
});
