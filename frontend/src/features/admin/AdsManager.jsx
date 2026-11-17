import ConfirmDialog from "../../shared/ui/Confirmation";
import DataTable from "../../shared/ui/DataTable";
import { Metric, MetricGrid } from "../../shared/ui/Metrics";
import { SectionHeading } from "../../shared/ui/Layout";
import { EmptyState } from "../../shared/ui/Feedback";
import { Input, TextArea, Field, Checkbox } from "../../shared/ui/FormControls";
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
import { BarChart } from "./ConsoleCharts";
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
      <SectionHeading>
        <h2>Рекламные кампании</h2>
        <Button variant="primary" onClick={() => setEditing({})}>
          <Plus size={16} /> Новая кампания
        </Button>
      </SectionHeading>
      <Status
        error={metrics.error}
        loading={metrics.isPending}
        onRetry={metrics.refetch}
      >
        <MetricGrid>
          {[
            ["served", "Выдано размещений"],
            ["impressions", "Показы"],
            ["clicks", "Переходы"],
          ].map(([key, label]) => (
            <Metric
              key={key}
              label={<>{label}</>}
              value={<>{number(metrics.data?.[key])}</>}
            />
          ))}
        </MetricGrid>
      </Status>
      <p className="muted">
        Показ засчитывается после появления баннера на экране или начала
        рекламного видео. Выдача размещения учитывается отдельно.
      </p>
      {metrics.data && (
        <BarChart
          title="Результаты рекламы"
          description="Суммарные счётчики за всё время. Выдачи, показы и переходы учитываются отдельно."
          rows={[
            ["Выдано размещений", metrics.data.served],
            ["Показы", metrics.data.impressions],
            ["Переходы", metrics.data.clicks],
          ]}
        />
      )}
      <Status
        loading={campaigns.isPending}
        error={!campaigns.data && campaigns.error}
        onRetry={campaigns.refetch}
      >
        <DataTable label="Рекламные кампании">
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
                    <Button size="compact" onClick={() => setEditing(row)}>
                      <Pencil size={14} /> Изменить
                    </Button>
                    <Button
                      size="compact"
                      onClick={() => {
                        remove.reset();
                        setDeleting(row);
                      }}
                    >
                      <Trash2 size={14} /> Удалить
                    </Button>
                  </div>
                </td>
              </tr>
            ))}
          </tbody>
        </DataTable>

        {pageItems(campaigns, "campaigns").length === 0 && (
          <EmptyState as="p">Кампаний пока нет.</EmptyState>
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
      <ConfirmDialog
        open={Boolean(deleting)}
        onClose={() => setDeleting(null)}
        title="Удалить кампанию"
        pending={remove.isPending}
        onConfirm={() => remove.mutate(deleting.id)}
        confirmLabel={<>Удалить</>}
        onCancel={() => setDeleting(null)}
        cancelLabel={<>Отмена</>}
        error={remove.error}
      >
        <p>
          Кампания больше не будет показываться. Накопленная статистика
          сохранится.
        </p>
      </ConfirmDialog>
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
      <Field>
        Внутреннее название
        <Input
          name="name"
          defaultValue={initial.name}
          maxLength={120}
          required
        />
      </Field>
      <Field>
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
      </Field>
      <Field>
        Заголовок
        <Input
          name="title"
          defaultValue={initial.title}
          maxLength={180}
          required
        />
      </Field>
      <Field>
        Описание
        <TextArea name="body" defaultValue={initial.body} maxLength={1000} />
      </Field>
      <div className="form-grid">
        <Field>
          Рекламодатель
          <Input
            name="advertiser"
            defaultValue={initial.advertiser}
            maxLength={180}
            required
          />
        </Field>
        <Field>
          Токен маркировки (erid)
          <Input
            name="disclosure_token"
            defaultValue={initial.disclosure_token}
            maxLength={200}
          />
        </Field>
      </div>
      <Field>
        Изображение (HTTPS)
        <Input
          name="image_url"
          type="url"
          defaultValue={initial.image_url}
          required={type !== "preroll"}
        />
      </Field>
      {type === "preroll" && (
        <Field>
          Видео (HTTPS)
          <Input
            name="video_url"
            type="url"
            defaultValue={initial.video_url}
            required
          />
        </Field>
      )}
      <Field>
        Ссылка рекламодателя (HTTPS)
        <Input
          name="click_url"
          type="url"
          defaultValue={initial.click_url}
          required
        />
      </Field>
      <small>
        Медиа должны находиться на разрешённых оператором доменах. Видео
        показывается отдельным блоком перед плеером Twitch.
      </small>
      <div className="form-grid">
        <Field>
          Начало
          <Input
            name="starts_at"
            type="datetime-local"
            defaultValue={localDate(initial.starts_at)}
          />
        </Field>
        <Field>
          Окончание
          <Input
            name="ends_at"
            type="datetime-local"
            defaultValue={localDate(initial.ends_at)}
          />
        </Field>
        <Field>
          Приоритет (0–1000)
          <Input
            name="priority"
            type="number"
            min={0}
            max={1000}
            defaultValue={initial.priority ?? 100}
          />
        </Field>
        <Field>
          Показов пользователю в сутки
          <Input
            name="daily_cap"
            type="number"
            min={1}
            max={100}
            defaultValue={initial.daily_cap ?? 3}
          />
        </Field>
        <Field>
          Пауза между выдачами, секунд
          <Input
            name="cooldown_seconds"
            type="number"
            min={0}
            max={86400}
            defaultValue={initial.cooldown_seconds ?? 300}
          />
        </Field>
        <Field>
          Пропуск видео через, секунд
          <Input
            name="skip_after_seconds"
            type="number"
            min={0}
            max={5}
            defaultValue={initial.skip_after_seconds ?? 3}
          />
        </Field>
      </div>
      <Checkbox name="enabled" defaultChecked={initial.enabled ?? false}>
        {" "}
        Кампания включена
      </Checkbox>
      {save.error && <Status error={save.error} />}
      <Button variant="primary" busy={save.isPending}>
        Сохранить кампанию
      </Button>
    </form>
  );
}
