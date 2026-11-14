import { Component, useEffect, useRef } from "react";
import { AlertCircle, ArrowRight, Loader2, Play, X } from "lucide-react";
import { Link } from "react-router-dom";

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
      {busy && <Loader2 size={16} className="spinner" />}
      {children}
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
export function Dialog({ open, onClose, title, children, className = "" }) {
  const ref = useRef(null);
  const previous = useRef(null);
  const closeRef = useRef(onClose);
  closeRef.current = onClose;
  const openRef = useRef(open);
  openRef.current = open;
  useEffect(() => {
    const dialog = ref.current;
    if (open && !dialog.open) {
      previous.current = document.activeElement;
      dialog.showModal();
    }
    if (!open && dialog.open) dialog.close();
  }, [open]);
  return (
    <dialog
      ref={ref}
      className={`dialog ${className}`}
      aria-label={title}
      onCancel={(event) => {
        event.preventDefault();
        closeRef.current();
      }}
      onClose={() => {
        if (!openRef.current) previous.current?.focus?.();
      }}
      onClick={(event) => {
        if (event.target === ref.current) closeRef.current();
      }}
    >
      <div className="dialog-heading">
        <h2>{title}</h2>
        <IconButton label="Закрыть" onClick={onClose}>
          <X size={20} />
        </IconButton>
      </div>
      {children}
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
