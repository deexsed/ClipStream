import React from "react";
import { it, expect } from "vitest";
import { render, screen } from "@testing-library/react";
import { MemoryRouter, Routes, Route, Outlet } from "react-router-dom";
import LegalPage from "../src/features/legal/LegalPage";
import { documents } from "../src/features/legal/documents";

function show(path, documentsOverride = {}) {
  render(
    <MemoryRouter initialEntries={[path]}>
      <Routes>
        <Route
          element={
            <Outlet
              context={{
                config: {
                  legal: {
                    version: "2026-11-14",
                    documents: documentsOverride,
                  },
                },
                configQuery: {},
              }}
            />
          }
        >
          <Route path="/legal/:slug" element={<LegalPage />} />
        </Route>
      </Routes>
    </MemoryRouter>,
  );
}
it.each(Object.keys(documents))(
  "renders substantive built-in document %s without operator details",
  (slug) => {
    show(`/legal/${slug}`);
    expect(
      screen.getByRole("heading", { name: documents[slug].title }),
    ).toBeTruthy();
    expect(screen.getByText(documents[slug].sections[0][1])).toBeTruthy();
    expect(screen.queryByText("Документы ещё не опубликованы")).toBeNull();
    expect(screen.queryByRole("link", { name: "[указать email]" })).toBeNull();
  },
);
it("uses an explicitly published external edition", () => {
  show("/legal/privacy", { privacy: "https://example.org/privacy" });
  expect(
    screen
      .getByRole("link", { name: /Открыть опубликованную/ })
      .getAttribute("href"),
  ).toBe("https://example.org/privacy");
});
it("never renders an unsafe configured scheme as a link", () => {
  show("/legal/privacy", { privacy: "javascript:alert(1)" });
  expect(
    screen.queryByRole("link", { name: /Открыть опубликованную/ }),
  ).toBeNull();
  expect(screen.getByText(documents.privacy.sections[0][1])).toBeTruthy();
});
