export function isKeyboardNavigation(event) {
  if (event.key === "Tab" || event.key === "Escape") return true;
  const editing = event.target?.closest?.(
    'textarea,input:not([type="checkbox"]):not([type="radio"]):not([type="range"]),[contenteditable]:not([contenteditable="false"])',
  );
  return (
    !editing &&
    [
      "ArrowUp",
      "ArrowDown",
      "ArrowLeft",
      "ArrowRight",
      "Home",
      "End",
      "PageUp",
      "PageDown",
      "Enter",
      " ",
    ].includes(event.key)
  );
}

export function isSearchShortcut(event, platform) {
  const apple = /Mac|iPhone|iPad|iPod/i.test(platform || "");
  return Boolean(
    !event.altKey &&
    !event.shiftKey &&
    !event.isComposing &&
    !event.defaultPrevented &&
    !event.repeat &&
    (apple
      ? event.metaKey && !event.ctrlKey
      : event.ctrlKey && !event.metaKey) &&
    (event.code === "KeyK" || event.key?.toLowerCase() === "k"),
  );
}
