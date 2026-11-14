// Focus origins belong to native dialog elements and expire with them. A weak
// registry lets a replacement dialog inherit the opener of a dismissing one.
const origins = new WeakMap();

function resolveOrigin(target) {
  const visited = new Set();
  let owner = target?.closest?.("dialog");
  while (owner && !visited.has(owner)) {
    visited.add(owner);
    const origin = origins.get(owner);
    if (!origin || (owner.isConnected && owner.open && origin.isOpen())) break;
    target = origin.target;
    owner = target?.closest?.("dialog");
  }
  return target;
}

export function rememberDialogFocus(dialog, isOpen) {
  const active = document.activeElement;
  const target = dialog.contains(active)
    ? origins.get(dialog)?.target
    : resolveOrigin(active);
  origins.set(dialog, { target, isOpen });
}

export function restoreDialogFocus(dialog) {
  const active = document.activeElement;
  // A later panel or an intentional focus change owns focus now.
  if (active !== document.body && active !== dialog && !dialog.contains(active))
    return;
  const target = resolveOrigin(origins.get(dialog)?.target);
  if (
    !target?.isConnected ||
    target.closest("[inert], [hidden], dialog:not([open])") ||
    target.disabled
  )
    return;
  const other = [...document.querySelectorAll("dialog[open]")].find(
    (element) => element !== dialog && origins.get(element)?.isOpen(),
  );
  if (other && !other.contains(target)) return;
  target.focus({ preventScroll: true });
}
