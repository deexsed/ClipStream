import DataTable from "../../shared/ui/DataTable";
import { Surface } from "../../shared/ui/Layout";
import { EmptyState } from "../../shared/ui/Feedback";
import { useId } from "react";
import {
  BarChart as Chart,
  Bar,
  CartesianGrid,
  ResponsiveContainer,
  XAxis,
  YAxis,
  Tooltip,
} from "recharts";

const format = (value) => new Intl.NumberFormat("ru-RU").format(value);
const compact = (value) =>
  new Intl.NumberFormat("ru-RU", { notation: "compact" }).format(value);
const tooltipStyle = {
  background: "var(--panel)",
  border: "1px solid var(--line)",
  borderRadius: 10,
  color: "inherit",
};
const axis = { fill: "var(--muted)", fontSize: 12 };
export function BarChart({ title, description, rows }) {
  const id = useId();
  const points = rows
    .filter(
      ([, value]) =>
        typeof value === "number" && Number.isFinite(value) && value >= 0,
    )
    .map(([label, value]) => ({ label, value }));
  return (
    <Surface as="section" className="console-chart" aria-labelledby={id}>
      <h3 id={id}>{title}</h3>
      {description && <p className="muted">{description}</p>}
      {points.length ? (
        <>
          <div
            className="console-rechart"
            role="region"
            aria-label={`График: ${title}`}
          >
            <ResponsiveContainer
              width="100%"
              height={Math.max(200, points.length * 48)}
            >
              <Chart
                data={points}
                layout="vertical"
                margin={{ left: 0, right: 14, top: 10, bottom: 0 }}
                accessibilityLayer
              >
                <CartesianGrid
                  horizontal={false}
                  stroke="var(--line)"
                  strokeDasharray="3 3"
                />
                <XAxis
                  type="number"
                  allowDecimals={false}
                  tick={axis}
                  tickFormatter={compact}
                  axisLine={false}
                  tickLine={false}
                />
                <YAxis
                  type="category"
                  dataKey="label"
                  width={114}
                  tick={axis}
                  tickFormatter={(label) =>
                    label.length > 17 ? label.slice(0, 16) + "…" : label
                  }
                  axisLine={false}
                  tickLine={false}
                />
                <Tooltip
                  formatter={(value) => [format(value), "Количество"]}
                  contentStyle={tooltipStyle}
                  itemStyle={{ color: "var(--accent)" }}
                  cursor={{ fill: "var(--raised)" }}
                  isAnimationActive={false}
                />
                <Bar
                  dataKey="value"
                  fill="var(--accent)"
                  radius={[0, 4, 4, 0]}
                  maxBarSize={20}
                  isAnimationActive={false}
                />
              </Chart>
            </ResponsiveContainer>
          </div>
          <details>
            <summary>Точные значения</summary>
            <DataTable label="Точные значения графика" minWidth={0} compact>
              <thead>
                <tr>
                  <th>Показатель</th>
                  <th>Значение</th>
                </tr>
              </thead>
              <tbody>
                {points.map(({ label, value }) => (
                  <tr key={label}>
                    <td>{label}</td>
                    <td>{format(value)}</td>
                  </tr>
                ))}
              </tbody>
            </DataTable>
          </details>
        </>
      ) : (
        <EmptyState as="p">Данных пока нет</EmptyState>
      )}
    </Surface>
  );
}
export function HourlyChart({ points }) {
  const valid = points.filter(
    (point) =>
      Number.isFinite(Date.parse(point.hour)) &&
      typeof point.inserted === "number" &&
      point.inserted >= 0,
  );
  return (
    <div
      className="console-rechart"
      role="region"
      aria-label="График новых клипов по часам за последние 24 часа"
    >
      {valid.length ? (
        <ResponsiveContainer width="100%" height={250}>
          <Chart
            data={valid}
            margin={{ left: -16, right: 8, top: 16, bottom: 0 }}
            accessibilityLayer
          >
            <CartesianGrid
              vertical={false}
              stroke="var(--line)"
              strokeDasharray="3 3"
            />
            <XAxis
              dataKey="hour"
              tick={axis}
              tickFormatter={(value) =>
                new Date(value).toLocaleTimeString("ru-RU", {
                  hour: "2-digit",
                  minute: "2-digit",
                })
              }
              minTickGap={24}
              axisLine={false}
              tickLine={false}
            />
            <YAxis
              tick={axis}
              allowDecimals={false}
              tickFormatter={compact}
              axisLine={false}
              tickLine={false}
            />
            <Tooltip
              labelFormatter={(value) =>
                new Date(value).toLocaleString("ru-RU")
              }
              formatter={(value) => [format(value), "Новых клипов"]}
              contentStyle={tooltipStyle}
              itemStyle={{ color: "var(--accent)" }}
              cursor={{ fill: "var(--raised)" }}
              isAnimationActive={false}
            />
            <Bar
              dataKey="inserted"
              fill="var(--accent)"
              radius={[3, 3, 0, 0]}
              maxBarSize={32}
              isAnimationActive={false}
            />
          </Chart>
        </ResponsiveContainer>
      ) : (
        <EmptyState as="p">Истории импорта пока нет</EmptyState>
      )}
    </div>
  );
}
