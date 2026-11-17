import { Button, Dialog, Status } from "./Primitives";

export function ConfirmationActions({
  pending = false,
  onConfirm,
  onCancel,
  confirmLabel = "Подтвердить",
  cancelLabel = "Отмена",
  className = "dialog-actions",
}) {
  return (
    <div className={className}>
      <Button type="button" disabled={pending} onClick={onCancel}>
        {cancelLabel}
      </Button>
      <Button
        type="button"
        variant="primary"
        busy={pending}
        onClick={() => {
          if (!pending) onConfirm();
        }}
      >
        {confirmLabel}
      </Button>
    </div>
  );
}
export default function ConfirmDialog({
  open,
  onClose,
  title,
  children,
  error,
  pending = false,
  onConfirm,
  onCancel,
  confirmLabel,
  cancelLabel,
  className,
}) {
  const close = () => {
    if (!pending) onClose();
  };
  return (
    <Dialog open={open} onClose={close} title={title} className={className}>
      {children}
      {error && <Status error={error} />}
      <ConfirmationActions
        pending={pending}
        onCancel={() => {
          if (!pending) (onCancel || onClose)();
        }}
        onConfirm={onConfirm}
        confirmLabel={confirmLabel}
        cancelLabel={cancelLabel}
      />
    </Dialog>
  );
}
