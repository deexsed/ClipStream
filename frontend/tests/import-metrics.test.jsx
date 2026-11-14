import React from "react";
import { render, screen, within } from "@testing-library/react";
import { expect, it } from "vitest";
import ImportMetrics from "../src/features/admin/ImportMetrics";

it("distinguishes platform imports, updates, Twitch creation time and local deferrals", () => {
  render(
    <ImportMetrics
      data={{
        catalog: {
          total: 27648,
          visible: 27000,
          added_24h: 123,
          created_on_twitch_4h: 20,
          categories_known: 200,
        },
        hourly: [
          { hour: "2026-11-14T10:00:00Z", inserted: 0 },
          { hour: "2026-11-14T11:00:00Z", inserted: 123 },
        ],
        phases: [
          {
            phase: "fresh",
            inserted: 123,
            updated: 45,
            deferred: 2,
            twitch_429: 0,
            last_activity_at: new Date().toISOString(),
          },
        ],
        rate: {
          available: true,
          requests_last_minute: 130,
          background_last_minute: 120,
          total_budget: 600,
          background_budget: 120,
          app_remaining: -1,
          background_reserve_percent: 25,
        },
      }}
    />,
  );
  expect(screen.getByText("27 648", { exact: false })).toBeTruthy();
  expect(screen.getByText("Созданы на Twitch за 4 часа")).toBeTruthy();
  expect(screen.getByRole("img", { name: /График новых клипов/ })).toBeTruthy();
  expect(screen.getByText("Нет свежих заголовков")).toBeTruthy();
  expect(
    within(
      screen.getByText("Свежие · от 5 просмотров").closest("tr"),
    ).getByText("45"),
  ).toBeTruthy();
  expect(screen.getByRole("status").textContent).toContain(
    "Worker обрабатывает очередь",
  );
});

it("shows missing worker activity and unavailable rate telemetry explicitly", () => {
  render(
    <ImportMetrics
      data={{ catalog: {}, phases: [], rate: { available: false } }}
    />,
  );
  expect(screen.getByText(/Нет активности импорта/)).toBeTruthy();
  expect(screen.getByText("Данные бюджета временно недоступны.")).toBeTruthy();
});
