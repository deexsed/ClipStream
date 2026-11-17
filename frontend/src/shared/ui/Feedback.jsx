import "./Feedback.css";

export function Feedback({
  as: Tag = "p",
  className = "",
  children,
  ...props
}) {
  return (
    <Tag role="status" {...props} className={`feedback ${className}`}>
      {children}
    </Tag>
  );
}
export function EmptyState({
  as: Tag = "div",
  icon: Icon,
  title,
  description,
  className = "",
  children,
  ...props
}) {
  return (
    <Tag {...props} className={`empty-state ${className}`}>
      {Icon && <Icon size={28} strokeWidth={1.3} aria-hidden="true" />}
      {title && <h3>{title}</h3>}
      {description && <p>{description}</p>}
      {children}
    </Tag>
  );
}
