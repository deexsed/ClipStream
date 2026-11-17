import { act, render, screen, within } from "@testing-library/react";
import { MemoryRouter, Route, Routes } from "react-router-dom";
import { expect, it, vi } from "vitest";
import { PublicPageLayout } from "../src/app/SiteFooter";

it("updates the copyright year at local midnight on an open page", () => {
  vi.useFakeTimers();
  const newYear = new Date(2030, 0, 1);
  vi.setSystemTime(newYear.getTime() - 1_000);

  const view = render(
    <MemoryRouter>
      <Routes>
        <Route element={<PublicPageLayout />}>
          <Route index element={<h1>Лента</h1>} />
        </Route>
      </Routes>
    </MemoryRouter>,
  );

  try {
    const footer = within(screen.getByRole("contentinfo"));
    expect(footer.getByText("© 2029")).toBeTruthy();
    act(() => vi.advanceTimersByTime(1_000));
    expect(footer.getByText("© 2030")).toBeTruthy();
  } finally {
    view.unmount();
    vi.useRealTimers();
  }
});
