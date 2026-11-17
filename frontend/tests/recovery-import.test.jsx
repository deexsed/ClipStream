import React from "react";
import { render, screen } from "@testing-library/react";
import { expect, it } from "vitest";
import ImportMetrics from "../src/features/admin/ImportMetrics";

it("distinguishes low-threshold outage recovery from ordinary archive history", () => {
  render(
    <ImportMetrics
      data={{
        catalog: {},
        rate: { available: false },
        phases: [
          { phase: "fresh", inserted: 2 },
          { phase: "recovery", inserted: 7, updated: 3 },
          { phase: "history", inserted: 4 },
        ],
        queue: [
          {
            phase: "recovery",
            pending: 9,
            oldest_window_utc: "2026-11-14 15:00:00",
          },
        ],
      }}
    />,
  );
  expect(
    screen.getByRole("row", { name: /После простоя · от 5 просмотров/ }),
  ).toBeTruthy();
  expect(
    screen.getByRole("row", { name: /История · от 50 просмотров/ }),
  ).toBeTruthy();
  expect(
    screen.getByText(/После простоя · от 5 просмотров: 9 окон/),
  ).toBeTruthy();
});
