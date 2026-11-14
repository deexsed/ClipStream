import "./ImportMetrics.css";
const number = (value) =>
  new Intl.NumberFormat("ru-RU").format(Number(value) || 0);

const phases = {
  fresh: "Свежие · от 5 просмотров",
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
  const maximum = Math.max(1, ...hourly.map((point) => point.inserted));
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
      <div className="section-label">
        <h2 id="import-title">Каталог и импорт Twitch</h2>
        <span className="muted">Обновляется каждые 30 секунд</span>
      </div>
      <div className="metric-grid">
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
          <div className="surface metric" key={label}>
            <span>{label}</span>
            <strong>{number(value)}</strong>
          </div>
        ))}
      </div>
      <div className="surface">
        <h3>Новые клипы по часам · последние 24 часа</h3>
        <p className="muted">
          Время добавления в нашу базу. Часы показаны в вашем часовом поясе.
        </p>
        <div
          className="import-chart"
          role="img"
          aria-label="График новых клипов по часам за последние 24 часа"
        >
          {hourly.map((point) => (
            <div
              className="import-chart-column"
              key={point.hour}
              title={`${time(point.hour)}: ${number(point.inserted)} клипов`}
            >
              <span
                className="import-chart-bar"
                style={{ height: `${(point.inserted / maximum) * 100}%` }}
              />
              <span className="import-chart-hour">
                {new Date(point.hour).getHours()}
              </span>
            </div>
          ))}
        </div>
        <details>
          <summary>Точные значения по часам</summary>
          <div className="import-table-scroll">
            <table className="import-table">
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
            </table>
          </div>
        </details>
      </div>
      <div className="surface">
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
        <div className="import-table-scroll">
          <table className="import-table">
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
          </table>
        </div>
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
      </div>
      <div className="surface">
        <h3>Бюджет Twitch API</h3>
        {rate.available ? (
          <>
            <div className="metric-grid">
              <div className="metric">
                <span>Все запросы · скользящая минута</span>
                <strong>
                  {number(rate.requests_last_minute)} /{" "}
                  {number(rate.total_budget)}
                </strong>
              </div>
              <div className="metric">
                <span>Импорт · скользящая минута</span>
                <strong>
                  {number(rate.background_last_minute)} /{" "}
                  {number(rate.background_budget)}
                </strong>
              </div>
              <div className="metric">
                <span>Остаток app bucket по заголовкам Twitch</span>
                <strong>
                  {rate.app_remaining >= 0
                    ? `${number(rate.app_remaining)} / ${number(rate.app_limit)}`
                    : "Нет свежих заголовков"}
                </strong>
              </div>
            </div>
            <p className="muted">
              Импорт уступает пользовательским запросам и сохраняет{" "}
              {rate.background_reserve_percent}% app bucket. При нехватке
              бюджета задачи откладываются.
            </p>
          </>
        ) : (
          <p role="status">Данные бюджета временно недоступны.</p>
        )}
      </div>
      <div className="surface">
        <h3>Крупнейшие категории каталога</h3>
        <div className="import-table-scroll">
          <table className="import-table">
            <thead>
              <tr>
                <th>Категория</th>
                <th>Клипов</th>
              </tr>
            </thead>
            <tbody>
              {(data.categories || []).map((g) => (
                <tr key={g.name}>
                  <td>{g.name}</td>
                  <td>{number(g.clips)}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      </div>
    </section>
  );
}
