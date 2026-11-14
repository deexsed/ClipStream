import React from "react";
import { it, expect, vi } from "vitest";
import {
  render,
  screen,
  fireEvent,
  waitFor,
  act,
} from "@testing-library/react";
import { QueryClientProvider } from "@tanstack/react-query";
import { queryClient } from "../src/shared/api/queries";
import { Preroll } from "../src/features/ads/Advertisement";
function renderAd() {
  return render(
    <QueryClientProvider client={queryClient}>
      <Preroll clipId={123}>
        <div>Clip player</div>
      </Preroll>
    </QueryClientProvider>,
  );
}
const offer = {
  ad: {
    id: 1,
    type: "preroll",
    title: "Test advertisement",
    advertiser: "Test advertiser",
    video_url: "https://media.example.test/ad.mp4",
    click_url: "https://example.test",
    skip_after_seconds: 3,
  },
  serve_token: "proof",
};
it("opens the clip when advertisement inventory is empty", async () => {
  vi.spyOn(globalThis, "fetch").mockResolvedValue(
    new Response(JSON.stringify({ ad: null })),
  );
  renderAd();
  await screen.findByText("Clip player");
  expect(screen.queryByLabelText("Реклама перед клипом")).toBeNull();
});
it("does not hold a clip after an advertisement request fails", async () => {
  vi.spyOn(globalThis, "fetch").mockResolvedValue(
    new Response("{}", { status: 503 }),
  );
  renderAd();
  await screen.findByText("Clip player");
});
it("labels a preroll and falls back to the clip when video fails", async () => {
  vi.spyOn(globalThis, "fetch").mockResolvedValue(
    new Response(JSON.stringify(offer)),
  );
  renderAd();
  await screen.findByLabelText("Реклама перед клипом");
  expect(screen.getByText("Реклама · Test advertiser")).toBeTruthy();
  expect(screen.queryByText("Clip player")).toBeNull();
  fireEvent.error(document.querySelector("video"));
  await screen.findByText("Clip player");
});
it("allows skipping a preroll before playback without reporting a fake impression", async () => {
  const fetch = vi
    .spyOn(globalThis, "fetch")
    .mockResolvedValue(new Response(JSON.stringify(offer)));
  renderAd();
  await screen.findByLabelText("Реклама перед клипом");
  fireEvent.click(screen.getByText("Пропустить рекламу без воспроизведения"));
  await screen.findByText("Clip player");
  expect(
    fetch.mock.calls.filter(([url]) => url.includes("/ads/events")),
  ).toHaveLength(0);
});
it("records an advertisement only after video playback actually starts", async () => {
  const fetch = vi
    .spyOn(globalThis, "fetch")
    .mockImplementation(
      async (url) =>
        new Response(
          JSON.stringify(
            url.includes("/ads/serve") ? offer : { recorded: true },
          ),
        ),
    );
  renderAd();
  await screen.findByLabelText("Реклама перед клипом");
  expect(
    fetch.mock.calls.filter(([url]) => url.includes("/ads/events")),
  ).toHaveLength(0);
  fireEvent.playing(document.querySelector("video"));
  await waitFor(() =>
    expect(
      fetch.mock.calls.filter(([url]) => url.includes("/ads/events")),
    ).toHaveLength(1),
  );
});
