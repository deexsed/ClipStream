import "./Metrics.css";
import "./Layout.css";

export function MetricGrid({ compact = false, className = "", ...props }) {
  return (
    <div
      {...props}
      className={`${compact ? "profile-stats" : "metric-grid"} ${className}`}
    />
  );
}
export function Metric({
  label,
  value,
  surface = true,
  compact = false,
  className = "",
  ...props
}) {
  return (
    <div
      {...props}
      className={`${surface && !compact ? "surface" : ""} metric ${compact ? "metric-compact" : ""} ${className}`}
    >
      {compact ? (
        <>
          <strong>{value}</strong>
          <span>{label}</span>
        </>
      ) : (
        <>
          <span>{label}</span>
          <strong>{value}</strong>
        </>
      )}
    </div>
  );
}
