import Select from "../../shared/ui/Select";
import { useState } from "react";
import { useMutation } from "@tanstack/react-query";
import { Plus, Pencil, Trash2 } from "lucide-react";
import { request } from "../../shared/api/http";
import {
  useApi,
  usePages,
  pageItems,
  queryClient,
} from "../../shared/api/queries";
import { Button, Dialog, LoadMore, Status } from "../../shared/ui/Primitives";
import { date, number } from "../../shared/format";
const types = {
  banner: "Баннер",
  feed: "Вставка в ленту",
  preroll: "Перед клипом",
};
const fields = [
  "name",
  "type",
  "title",
  "body",
  "advertiser",
  "disclosure_token",
  "image_url",
  "video_url",
  "click_url",
  "enabled",
  "priority",
  "starts_at",
  "ends_at",
  "daily_cap",
  "cooldown_seconds",
  "skip_after_seconds",
];
export default function AdsManager() {
  const campaigns = usePages("/admin/ads", "campaigns", {}, { pageSize: 30 });
  const metrics = useApi("/admin/ads/metrics");
  const [editing, setEditing] = useState(null);
  const [deleting, setDeleting] = useState(null);
  const refresh = () =>
    queryClient.invalidateQueries({
      predicate: (query) => String(query.queryKey[1]).startsWith("/admin/ads"),
    });
  const remove = useMutation({
    mutationFn: (id) => request(`/admin/ads/${id}`, { method: "DELETE" }),
    onSuccess: () => {
      setDeleting(null);
      refresh();
    },
  });
  return (
    <section>
      <div className="section-label">
        <h2>Рекламные кампании</h2>
        <Button variant="primary" onClick={() => setEditing({})}>
          <Plus size={16} /> Новая кампания
        </Button>
      </div>
      <Status
        error={metrics.error}
        loading={metrics.isPending}
        onRetry={metrics.refetch}
      >
        <div className="metric-grid">
          {[
            ["served", "Выдано размещений"],
            ["impressions", "Показы"],
            ["clicks", "Переходы"],
          ].map(([key, label]) => (
            <div className="surface metric" key={key}>
              <span>{label}</span>
              <strong>{number(metrics.data?.[key])}</strong>
            </div>
          ))}
        </div>
      </Status>
      <p className="muted">
        Показ засчитывается после появления баннера на экране или начала
        рекламного видео. Выдача размещения учитывается отдельно.
      </p>
      <Status
        loading={campaigns.isPending}
        error={!campaigns.data && campaigns.error}
        onRetry={campaigns.refetch}
      >
        <div className="table-scroll">
          <table>
            <thead>
              <tr>
                <th>Кампания</th>
                <th>Формат</th>
                <th>Статус</th>
                <th>Показы / клики</th>
                <th>Окончание</th>
                <th>Действия</th>
              </tr>
            </thead>
            <tbody>
              {pageItems(campaigns, "campaigns").map((row) => (
                <tr key={row.id}>
                  <td>
                    <strong>{row.name}</strong>
                    <small className="block">{row.advertiser}</small>
                  </td>
                  <td>{types[row.type]}</td>
                  <td>{row.enabled ? "Включена" : "На паузе"}</td>
                  <td>
                    {number(row.metrics?.impressions)} /{" "}
                    {number(row.metrics?.clicks)}
                  </td>
                  <td>{date(row.ends_at, true) || "Без срока"}</td>
                  <td>
                    <div className="button-row">
                      <Button onClick={() => setEditing(row)}>
                        <Pencil size={14} /> Изменить
                      </Button>
                      <Button onClick={() => setDeleting(row)}>
                        <Trash2 size={14} /> Удалить
                      </Button>
                    </div>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
        {pageItems(campaigns, "campaigns").length === 0 && (
          <p className="empty-state">Кампаний пока нет.</p>
        )}
        <LoadMore query={campaigns} />
      </Status>
      <Dialog
        open={Boolean(editing)}
        onClose={() => setEditing(null)}
        title={editing?.id ? "Редактировать кампанию" : "Новая кампания"}
        className="dialog-wide"
      >
        {editing && (
          <CampaignForm
            key={editing.id || "new"}
            initial={editing}
            onSaved={() => {
              setEditing(null);
              refresh();
            }}
          />
        )}
      </Dialog>
      <Dialog
        open={Boolean(deleting)}
        onClose={() => setDeleting(null)}
        title="Удалить кампанию"
      >
        <p>
          Кампания больше не будет показываться. Накопленная статистика
          сохранится.
        </p>
        {remove.error && <Status error={remove.error} />}
        <div className="dialog-actions">
          <Button onClick={() => setDeleting(null)}>Отмена</Button>
          <Button
            variant="primary"
            busy={remove.isPending}
            onClick={() => remove.mutate(deleting.id)}
          >
            Удалить
          </Button>
        </div>
      </Dialog>
    </section>
  );
}
function localDate(value) {
  if (!value) return "";
  const d = new Date(
    value.includes("T") ? value : value.replace(" ", "T") + "Z",
  );
  if (!Number.isFinite(d.getTime())) return "";
  return new Date(d.getTime() - d.getTimezoneOffset() * 60000)
    .toISOString()
    .slice(0, 16);
}
function CampaignForm({ initial, onSaved }) {
  const [type, setType] = useState(initial.type || "banner");
  const save = useMutation({
    mutationFn: (body) =>
      request(`/admin/ads${initial.id ? "/" + initial.id : ""}`, {
        method: initial.id ? "PUT" : "POST",
        body,
      }),
    onSuccess: onSaved,
  });
  return (
    <form
      className="form-stack"
      onSubmit={(event) => {
        event.preventDefault();
        const form = Object.fromEntries(new FormData(event.currentTarget));
        const body = Object.fromEntries(
          fields.map((key) => [key, form[key] || ""]),
        );
        body.enabled = Boolean(form.enabled);
        [
          "priority",
          "daily_cap",
          "cooldown_seconds",
          "skip_after_seconds",
        ].forEach((key) => (body[key] = Number(form[key] || 0)));
        ["starts_at", "ends_at"].forEach(
          (key) =>
            (body[key] = form[key]
              ? new Date(form[key]).toISOString().replace(".000Z", "Z")
              : null),
        );
        if (type !== "preroll") body.video_url = "";
        save.mutate(body);
      }}
    >
      <label>
        Внутреннее название
        <input
          name="name"
          defaultValue={initial.name}
          maxLength={120}
          required
        />
      </label>
      <label>
        Формат
        <Select
          aria-label="Формат рекламы"
          name="type"
          value={type}
          onChange={(event) => setType(event.target.value)}
        >
          {Object.entries(types).map(([value, label]) => (
            <option key={value} value={value}>
              {label}
            </option>
          ))}
        </Select>
      </label>
      <label>
        Заголовок
        <input
          name="title"
          defaultValue={initial.title}
          maxLength={180}
          required
        />
      </label>
      <label>
        Описание
        <textarea name="body" defaultValue={initial.body} maxLength={1000} />
      </label>
      <div className="form-grid">
        <label>
          Рекламодатель
          <input
            name="advertiser"
            defaultValue={initial.advertiser}
            maxLength={180}
            required
          />
        </label>
        <label>
          Токен маркировки (erid)
          <input
            name="disclosure_token"
            defaultValue={initial.disclosure_token}
            maxLength={200}
          />
        </label>
      </div>
      <label>
        Изображение (HTTPS)
        <input
          name="image_url"
          type="url"
          defaultValue={initial.image_url}
          required={type !== "preroll"}
        />
      </label>
      {type === "preroll" && (
        <label>
          Видео (HTTPS)
          <input
            name="video_url"
            type="url"
            defaultValue={initial.video_url}
            required
          />
        </label>
      )}
      <label>
        Ссылка рекламодателя (HTTPS)
        <input
          name="click_url"
          type="url"
          defaultValue={initial.click_url}
          required
        />
      </label>
      <small>
        Медиа должны находиться на разрешённых оператором доменах. Видео
        показывается отдельным блоком перед плеером Twitch.
      </small>
      <div className="form-grid">
        <label>
          Начало
          <input
            name="starts_at"
            type="datetime-local"
            defaultValue={localDate(initial.starts_at)}
          />
        </label>
        <label>
          Окончание
          <input
            name="ends_at"
            type="datetime-local"
            defaultValue={localDate(initial.ends_at)}
          />
        </label>
        <label>
          Приоритет (0–1000)
          <input
            name="priority"
            type="number"
            min={0}
            max={1000}
            defaultValue={initial.priority ?? 100}
          />
        </label>
        <label>
          Показов пользователю в сутки
          <input
            name="daily_cap"
            type="number"
            min={1}
            max={100}
            defaultValue={initial.daily_cap ?? 3}
          />
        </label>
        <label>
          Пауза между выдачами, секунд
          <input
            name="cooldown_seconds"
            type="number"
            min={0}
            max={86400}
            defaultValue={initial.cooldown_seconds ?? 300}
          />
        </label>
        <label>
          Пропуск видео через, секунд
          <input
            name="skip_after_seconds"
            type="number"
            min={0}
            max={5}
            defaultValue={initial.skip_after_seconds ?? 3}
          />
        </label>
      </div>
      <label className="checkbox-label">
        <input
          name="enabled"
          type="checkbox"
          defaultChecked={initial.enabled ?? false}
        />{" "}
        Кампания включена
      </label>
      {save.error && <Status error={save.error} />}
      <Button variant="primary" busy={save.isPending}>
        Сохранить кампанию
      </Button>
    </form>
  );
}
