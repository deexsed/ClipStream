import DataTable from "../../shared/ui/DataTable";
import { Metric, MetricGrid } from "../../shared/ui/Metrics";
import { Surface, SectionHeading } from "../../shared/ui/Layout";
import "./ImportMetrics.css";
import { HourlyChart, BarChart } from "./ConsoleCharts";
const number = (value) =>
  new Intl.NumberFormat("ru-RU").format(Number(value) || 0);

const phases = {
  fresh: "Свежие · от 5 просмотров",
  recovery: "После простоя · от 5 просмотров",
  history: "История · от 50 просмотров",
  categories: "Категории",
};
const time = (value) =>
  value ? new Date(value).toLocaleString("ru-RU") : "Пока нет данных";

export default function ImportMetrics({ data }) {
  if (!data) return null;
  const catalog = data.catalog || {};
  const rate = data.rate || {};
  const hourly = data.hourly || [];
  const total = (key) =>
    (data.phases || []).reduce((sum, phase) => sum + (phase[key] || 0), 0);
  const lastActivity = (data.phases || [])
    .map((p) => p.last_activity_at)
    .filter(Boolean)
    .sort()
    .at(-1);
  const stale =
    !lastActivity || Date.now() - Date.parse(lastActivity) > 10 * 60 * 1000;
  return (
    <section className="import-metrics" aria-labelledby="import-title">
      <SectionHeading>
        <h2 id="import-title">Каталог и импорт Twitch</h2>
        <span className="muted">Обновляется каждые 30 секунд</span>
      </SectionHeading>
      <MetricGrid>
        {[
          ["Клипов в базе", catalog.total],
          ["Доступно на платформе", catalog.visible],
          ["Добавлено за 24 часа", catalog.added_24h],
          ["Обновлено импортом за 24 часа", total("updated")],
          ["Категорий с клипами", catalog.categories_with_clips],
          ["Известных категорий", catalog.categories_known],
          ["Созданы на Twitch за 4 часа", catalog.created_on_twitch_4h],
          ["Отфильтровано за 24 часа", total("filtered")],
        ].map(([label, value]) => (
          <Metric
            key={label}
            label={<>{label}</>}
            value={<>{number(value)}</>}
          />
        ))}
      </MetricGrid>
      <Surface>
        <h3>Новые клипы по часам · последние 24 часа</h3>
        <p className="muted">
          Время добавления в нашу базу. Часы показаны в вашем часовом поясе.
        </p>
        <HourlyChart points={hourly} />
        <details>
          <summary>Точные значения по часам</summary>

          <DataTable label="Новые клипы по часам" minWidth={0} compact>
            <thead>
              <tr>
                <th>Час</th>
                <th>Новых клипов</th>
              </tr>
            </thead>
            <tbody>
              {hourly.map((p) => (
                <tr key={p.hour}>
                  <td>{time(p.hour)}</td>
                  <td>{number(p.inserted)}</td>
                </tr>
              ))}
            </tbody>
          </DataTable>
        </details>
      </Surface>
      <Surface>
        <h3>Состояние сбора</h3>
        <p role="status">
          {stale
            ? "Нет активности импорта более 10 минут или worker ещё не запускался."
            : "Worker обрабатывает очередь."}
        </p>
        <p className="muted">
          Последняя активность: {time(lastActivity)}. Последний новый клип:{" "}
          {time(catalog.last_import_at)}.
        </p>

        <DataTable label="Потоки импорта" compact>
          <thead>
            <tr>
              <th>Поток · за 24 часа</th>
              <th>Получено</th>
              <th>Новых</th>
              <th>Обновлено</th>
              <th>Без изменений</th>
              <th>Отсев</th>
              <th>Запросов</th>
              <th>Отложено</th>
              <th>Ошибок</th>
              <th>429 Twitch</th>
            </tr>
          </thead>
          <tbody>
            {(data.phases || []).map((p) => (
              <tr key={p.phase}>
                <th scope="row">{phases[p.phase] || p.phase}</th>
                {[
                  "fetched",
                  "inserted",
                  "updated",
                  "unchanged",
                  "filtered",
                  "requests",
                  "deferred",
                  "errors",
                  "twitch_429",
                ].map((key) => (
                  <td key={key}>{number(p[key])}</td>
                ))}
              </tr>
            ))}
          </tbody>
        </DataTable>

        <p className="muted">
          Счётчики импорта собираются с установки новой версии. Повторная
          обработка существующего клипа не увеличивает число новых.
        </p>
        {(data.queue || []).map((q) => (
          <p key={q.phase}>
            {phases[q.phase] || q.phase}: {number(q.pending)} окон в очереди.
            Старейшее окно (UTC): {q.oldest_window_utc}.
          </p>
        ))}
        {total("saturated") > 0 && (
          <p role="alert">
            Есть минутные окна, достигшие предела выдачи Twitch:{" "}
            {number(total("saturated"))}. Полнота сбора в них не гарантируется.
          </p>
        )}
      </Surface>
      <Surface>
        <h3>Бюджет Twitch API</h3>
        {rate.available ? (
          <>
            <MetricGrid>
              <Metric
                label={<>Все запросы · скользящая минута</>}
                value={
                  <>
                    {number(rate.requests_last_minute)} /{" "}
                    {number(rate.total_budget)}
                  </>
                }
                surface={false}
              />
              <Metric
                label={<>Импорт · скользящая минута</>}
                value={
                  <>
                    {number(rate.background_last_minute)} /{" "}
                    {number(rate.background_budget)}
                  </>
                }
                surface={false}
              />
              <Metric
                label={<>Остаток app bucket по заголовкам Twitch</>}
                value={
                  <>
                    {rate.app_remaining >= 0
                      ? `${number(rate.app_remaining)} / ${number(rate.app_limit)}`
                      : "Нет свежих заголовков"}
                  </>
                }
                surface={false}
              />
            </MetricGrid>
            <p className="muted">
              Импорт уступает пользовательским запросам и сохраняет{" "}
              {rate.background_reserve_percent}% app bucket. При нехватке
              бюджета задачи откладываются.
            </p>
          </>
        ) : (
          <p role="status">Данные бюджета временно недоступны.</p>
        )}
      </Surface>
      <BarChart
        title="Крупнейшие категории каталога"
        description="Количество клипов по категориям"
        rows={(data.categories || []).map((row) => [row.name, row.clips])}
      />
    </section>
  );
}
