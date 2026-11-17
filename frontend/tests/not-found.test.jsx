import React from "react";
import { expect, it, vi } from "vitest";
import { fireEvent, render, screen } from "@testing-library/react";
import { MemoryRouter, Outlet, useLocation } from "react-router-dom";
import App from "../src/app/App";

vi.mock("../src/app/Shell", () => ({ default: () => <Outlet /> }));
vi.mock("../src/features/discover/DiscoverPage", () => ({
  default: () => <h1>Каталог клипов</h1>,
}));

function Location() {
  return <output aria-label="Текущий адрес">{useLocation().pathname}</output>;
}

function show(path) {
  render(
    <MemoryRouter initialEntries={[path]}>
      <App />
      <Location />
    </MemoryRouter>,
  );
}

it.each(["/missing-page", "/missing/nested/page"])(
  "renders the 404 route for %s with working home navigation",
  async (path) => {
    show(path);
    expect(
      await screen.findByRole("heading", {
        level: 1,
        name: "Кажется, мы сбились с эфира",
      }),
    ).toBeTruthy();
    const home = screen.getByRole("link", { name: "На главную" });
    expect(home.getAttribute("href")).toBe("/");
    fireEvent.click(home);
    expect(
      await screen.findByRole("heading", { name: "Каталог клипов" }),
    ).toBeTruthy();
    expect(screen.getByLabelText("Текущий адрес").textContent).toBe("/");
  },
);

it("opens trends through client-side navigation", async () => {
  show("/unknown");
  const trends = await screen.findByRole("link", { name: "Смотреть тренды" });
  expect(trends.getAttribute("href")).toBe("/trends");
  fireEvent.click(trends);
  expect(
    await screen.findByRole("heading", { name: "Каталог клипов" }),
  ).toBeTruthy();
  expect(screen.getByLabelText("Текущий адрес").textContent).toBe("/trends");
});

it.each(["/feed", "/feed?start=123&clip_langs=ru"])(
  "redirects the retired watch route %s to the home page",
  async (path) => {
    show(path);
    expect(
      await screen.findByRole("heading", { name: "Каталог клипов" }),
    ).toBeTruthy();
    expect(screen.getByLabelText("Текущий адрес").textContent).toBe("/");
    expect(screen.queryByText("Кажется, мы сбились с эфира")).toBeNull();
  },
);
