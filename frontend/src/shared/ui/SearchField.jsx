import { forwardRef, useRef, useState } from "react";
import { Search, X } from "lucide-react";
import { IconButton } from "./Primitives";
import "./SearchField.css";

// The field owns its appearance and clear/focus behavior. Its parent owns
// submission, routing, and whether clearing should also reset applied filters.
export default forwardRef(function SearchField(
  {
    value,
    defaultValue = "",
    onValueChange,
    onClear,
    onKeyDown,
    className = "",
    clearLabel = "Очистить поиск",
    ...inputProps
  },
  forwardedRef,
) {
  const input = useRef(null);
  const [draft, setDraft] = useState(defaultValue);
  const text = value === undefined ? draft : value;
  const change = (next) => {
    if (value === undefined) setDraft(next);
    onValueChange?.(next);
  };
  const clear = () => {
    change("");
    onClear?.();
    input.current?.focus();
  };
  return (
    <div className={`search-field ${className}`}>
      <Search size={18} className="search-field-icon" aria-hidden="true" />
      <input
        {...inputProps}
        type="search"
        className="search-field-input"
        value={text}
        ref={(element) => {
          input.current = element;
          if (typeof forwardedRef === "function") forwardedRef(element);
          else if (forwardedRef) forwardedRef.current = element;
        }}
        autoComplete={inputProps.autoComplete ?? "off"}
        enterKeyHint="search"
        onChange={(event) => change(event.target.value)}
        onKeyDown={(event) => {
          onKeyDown?.(event);
          if (
            !event.defaultPrevented &&
            !inputProps.disabled &&
            !inputProps.readOnly &&
            event.key === "Escape" &&
            text &&
            !event.nativeEvent.isComposing
          ) {
            event.preventDefault();
            event.stopPropagation();
            clear();
          }
        }}
      />
      {text && (
        <IconButton
          type="button"
          label={clearLabel}
          className="search-field-clear"
          disabled={inputProps.disabled || inputProps.readOnly}
          onClick={clear}
        >
          <X size={16} aria-hidden="true" />
        </IconButton>
      )}
    </div>
  );
});
