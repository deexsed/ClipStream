import { forwardRef } from "react";
import "./FormControls.css";

export const Input = forwardRef(function Input(
  { className = "", ...props },
  ref,
) {
  return <input {...props} ref={ref} className={`ui-input ${className}`} />;
});
export const TextArea = forwardRef(function TextArea(
  { appearance, className = "", ...props },
  ref,
) {
  return (
    <textarea
      {...props}
      ref={ref}
      data-appearance={appearance}
      className={`ui-textarea ${className}`}
    />
  );
});
export function Field({ inline = false, className = "", children, ...props }) {
  return (
    <label
      {...props}
      className={`field ${inline ? "field-inline" : ""} ${className}`}
    >
      {children}
    </label>
  );
}
export const Checkbox = forwardRef(function Checkbox(
  { children, className = "", ...props },
  ref,
) {
  return (
    <label className={`checkbox-label ${className}`}>
      <input {...props} ref={ref} type="checkbox" className="ui-checkbox" />
      {children}
    </label>
  );
});
