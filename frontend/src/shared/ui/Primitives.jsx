import { Component, useLayoutEffect, useRef, useState } from "react";
import { AlertCircle, ArrowRight, Loader2, Play, X } from "lucide-react";
import { Link } from "react-router-dom";
import { motionSettings } from "./Reveal";
import { rememberDialogFocus, restoreDialogFocus } from "./dialogFocus";

export function Button({
  children,
  variant = "secondary",
  busy,
  className = "",
  ...props
}) {
  return (
    <button
      className={`button button-${variant} ${className}`}
      aria-busy={busy || undefined}
      {...props}
      disabled={props.disabled || busy}
    >
      {busy && (
        <span className="button-busy-indicator" aria-hidden="true">
          <Loader2 size={16} className="spinner" />
        </span>
      )}
      <span className="button-content">{children}</span>
    </button>
  );
}

export function IconButton({ label, children, ...props }) {
  return (
    <button className="icon-button" aria-label={label} title={label} {...props}>
      {children}
    </button>
  );
}

export function Status({ error, loading, empty, onRetry, children }) {
  if (error)
    return (
      <div className="status status-error" role="alert">
        <AlertCircle size={22} />
        <div>
          <h3>Не удалось загрузить</h3>
          <p>{error.message || String(error)}</p>
          {onRetry && <Button onClick={onRetry}>Попробовать снова</Button>}
        </div>
      </div>
    );
  if (loading)
    return (
      <div className="loading-state" role="status">
        <Loader2 className="spinner" size={22} /> Загрузка…
      </div>
    );
  if (empty)
    return (
      <div className="empty-state">
        <Play size={28} strokeWidth={1.3} />
        <h3>{empty}</h3>
      </div>
    );
  return children;
}

export function LoadMore({ query }) {
  if (query.isFetchNextPageError)
    return <Status error={query.error} onRetry={() => query.fetchNextPage()} />;
  return query.hasNextPage ? (
    <div className="load-more">
      <Button
        busy={query.isFetchingNextPage}
        onClick={() => query.fetchNextPage()}
      >
        Показать ещё <ArrowRight size={16} />
      </Button>
    </div>
  ) : null;
}

export function PageHeading({ eyebrow, title, description, children }) {
  return (
    <header className="page-heading">
      <div>
        {eyebrow && <p className="eyebrow">{eyebrow}</p>}
        <h1>{title}</h1>
        {description && <p className="muted">{description}</p>}
      </div>
      {children}
    </header>
  );
}

export function Avatar({ user, size = "", ...props }) {
  return user?.avatar_url || user?.profile_image_url ? (
    <img
      className={`avatar ${size}`}
      src={user.avatar_url || user.profile_image_url}
      alt=""
      {...props}
    />
  ) : (
    <span className={`avatar avatar-fallback ${size}`} aria-hidden="true">
      {Array.from(user?.username || user?.login || "?")[0].toUpperCase()}
    </span>
  );
}

// Native dialog owns focus containment, inert background, Escape and focus return.
export function Dialog({
  open,
  onClose,
  onAfterClose,
  title,
  children,
  className = "",
}) {
  const ref = useRef(null);
  const [present, setPresent] = useState(open);
  const closeRef = useRef(onClose);
  closeRef.current = onClose;
  const afterCloseRef = useRef(onAfterClose);
  afterCloseRef.current = onAfterClose;
  const openRef = useRef(open);
  openRef.current = open;
  const requestClose = () => {
    if (openRef.current) closeRef.current();
  };
  const animation = useRef(null);
  const closeNotified = useRef(true);
  // Keep conditionally rendered contents intact throughout the exit.
  const retained = useRef(children);
  if (open) retained.current = children;
  useLayoutEffect(() => {
    const dialog = ref.current;
    const interrupted = animation.current;
    const current = interrupted ? getComputedStyle(dialog) : null;
    const from = current && {
      opacity: current.opacity,
      transform: current.transform,
    };
    interrupted?.cancel();
    animation.current = null;
    if (open && !dialog.open) {
      rememberDialogFocus(dialog, () => openRef.current);
      closeNotified.current = false;
      dialog.showModal();
    }
    if (open) setPresent(true);
    if (!dialog.open) return;
    dialog.dataset.state = open ? "open" : "closing";
    const style = getComputedStyle(dialog);
    const settings = motionSettings(dialog, open, "dialog");
    if (settings.instant) {
      if (!open) {
        dialog.style.removeProperty("transform");
        dialog.close();
        setPresent(false);
      }
      return;
    }
    const hidden = {
      opacity: 0,
      transform:
        style.getPropertyValue("--dialog-hidden-transform").trim() ||
        "translateY(8px) scale(0.96)",
    };
    // A dragged sheet exits from the finger's last position, without snapping
    // back to its resting position first.
    const shown = { opacity: 1, transform: style.transform };
    const motion = dialog.animate(
      [
        from || (open ? hidden : shown),
        open ? { opacity: 1, transform: "none" } : hidden,
      ],
      settings,
    );
    animation.current = motion;
    motion.finished.then(
      () => {
        if (animation.current !== motion) return;
        animation.current = null;
        if (!openRef.current) {
          dialog.style.removeProperty("transform");
          dialog.close();
          setPresent(false);
        }
      },
      () => {},
    );
  }, [open]);
  useLayoutEffect(() => {
    const dialog = ref.current;
    return () => {
      const current = animation.current;
      animation.current = null;
      current?.cancel();
      if (dialog.open) dialog.close();
      restoreDialogFocus(dialog);
    };
  }, []);
  return (
    <dialog
      ref={ref}
      className={`dialog ${className}`}
      aria-label={title}
      onCancel={(event) => {
        event.preventDefault();
        requestClose();
      }}
      onClose={() => {
        if (!openRef.current && !ref.current.open && !closeNotified.current) {
          closeNotified.current = true;
          restoreDialogFocus(ref.current);
          afterCloseRef.current?.();
        }
      }}
      onClick={(event) => {
        if (event.target === ref.current) requestClose();
      }}
    >
      <div className="dialog-heading">
        <h2>{title}</h2>
        <IconButton label="Закрыть" onClick={requestClose}>
          <X size={20} />
        </IconButton>
      </div>
      {(open || present) && (open ? children : retained.current)}
    </dialog>
  );
}

export class ErrorBoundary extends Component {
  state = { error: null };
  static getDerivedStateFromError(error) {
    return { error };
  }
  render() {
    return this.state.error ? (
      <main className="page">
        <Status
          error={new Error("Не удалось открыть страницу.")}
          onRetry={() => window.location.reload()}
        />
        <Link className="text-link" to="/">
          На главную
        </Link>
      </main>
    ) : (
      this.props.children
    );
  }
}
