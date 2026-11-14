import React, { StrictMode, useState } from "react";
import { it, expect, vi } from "vitest";
import { render, screen, waitFor, fireEvent } from "@testing-library/react";
import { BrowserRouter, Routes, Route } from "react-router-dom";
import { QueryClientProvider } from "@tanstack/react-query";
import { queryClient } from "../src/shared/api/queries";
import { SessionProvider, useSession } from "../src/features/auth/Session";
import CallbackPage from "../src/features/auth/CallbackPage";

function Home() {
  const { user } = useSession();
  return <h1>{user?.username || "Guest"}</h1>;
}
function show() {
  return render(
    <StrictMode>
      <BrowserRouter>
        <QueryClientProvider client={queryClient}>
          <SessionProvider>
            <Routes>
              <Route path="/auth/callback" element={<CallbackPage />} />
              <Route path="/" element={<Home />} />
            </Routes>
          </SessionProvider>
        </QueryClientProvider>
      </BrowserRouter>
    </StrictMode>,
  );
}
it("completes a Twitch callback once while the router removes its query", async () => {
  window.history.replaceState(
    null,
    "",
    "/auth/callback?code=test-code&state=test-state",
  );
  sessionStorage.setItem(
    "clipstream.oauth",
    JSON.stringify({
      state: "test-state",
      verifier: "browser-proof",
      consent: { version: "test" },
    }),
  );
  const fetch = vi.spyOn(globalThis, "fetch").mockImplementation(
    async (url) =>
      new Response(
        JSON.stringify(
          url.endsWith("/auth/twitch")
            ? {
                token: "test-jwt",
                user: { id: 1, username: "Owner", role: 2 },
              }
            : { user: { id: 1, username: "Owner", role: 2 } },
        ),
      ),
  );
  show();
  await screen.findByRole("heading", { name: "Owner" });
  expect(
    fetch.mock.calls.filter(([url]) => url.endsWith("/auth/twitch")).length,
  ).toBe(1);
  expect(window.location.pathname).toBe("/");
  expect(sessionStorage.getItem("clipstream.oauth")).toBeNull();
});

it("retains an in-flight callback if its page remounts after query removal", async () => {
  window.history.replaceState(
    null,
    "",
    "/auth/callback?code=remount-code&state=remount-state",
  );
  sessionStorage.setItem(
    "clipstream.oauth",
    JSON.stringify({
      state: "remount-state",
      verifier: "browser-proof",
      consent: { version: "test" },
    }),
  );
  let complete;
  const fetch = vi.spyOn(globalThis, "fetch").mockImplementation((url) =>
    url.endsWith("/auth/twitch")
      ? new Promise((resolve) => {
          complete = () =>
            resolve(
              new Response(
                JSON.stringify({
                  token: "remount-jwt",
                  user: { id: 1, username: "Remounted", role: 2 },
                }),
              ),
            );
        })
      : Promise.resolve(
          new Response(
            JSON.stringify({ user: { id: 1, username: "Remounted", role: 2 } }),
          ),
        ),
  );
  function Remount() {
    const [key, setKey] = useState(0);
    return (
      <>
        <button onClick={() => setKey(key + 1)}>Remount callback</button>
        <CallbackPage key={key} />
      </>
    );
  }
  render(
    <BrowserRouter>
      <QueryClientProvider client={queryClient}>
        <SessionProvider>
          <Routes>
            <Route path="/auth/callback" element={<Remount />} />
            <Route path="/" element={<Home />} />
          </Routes>
        </SessionProvider>
      </QueryClientProvider>
    </BrowserRouter>,
  );
  await waitFor(() => expect(complete).toBeTypeOf("function"));
  expect(window.location.search).toBe("");
  fireEvent.click(screen.getByRole("button", { name: "Remount callback" }));
  complete();
  await screen.findByRole("heading", { name: "Remounted" });
  expect(
    fetch.mock.calls.filter(([url]) => url.endsWith("/auth/twitch")).length,
  ).toBe(1);
});

it("recovers a clean callback URL when the server confirms an existing session", async () => {
  window.history.replaceState(null, "", "/auth/callback");
  localStorage.setItem("token", "existing-jwt");
  const fetch = vi.spyOn(globalThis, "fetch").mockImplementation(
    async () =>
      new Response(
        JSON.stringify({
          user: { id: 1, username: "Existing owner", role: 2 },
        }),
      ),
  );
  show();
  await screen.findByRole("heading", { name: "Existing owner" });
  expect(fetch.mock.calls.some(([url]) => url.endsWith("/auth/twitch"))).toBe(
    false,
  );
});

it("rejects an unmatched browser state without exchanging a code", async () => {
  window.history.replaceState(
    null,
    "",
    "/auth/callback?code=invalid-code&state=unmatched",
  );
  sessionStorage.setItem(
    "clipstream.oauth",
    JSON.stringify({ state: "expected", verifier: "proof" }),
  );
  const fetch = vi.spyOn(globalThis, "fetch");
  show();
  await screen.findByText("Не удалось проверить запрос входа. Начните заново.");
  expect(fetch).not.toHaveBeenCalled();
});
