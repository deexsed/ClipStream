import React, { StrictMode, useState } from "react";
import { webcrypto } from "node:crypto";
import { describe, it, expect, vi } from "vitest";
import { render, screen, fireEvent, waitFor } from "@testing-library/react";
import { MemoryRouter, Routes, Route, Outlet } from "react-router-dom";
import { PreferencesProvider } from "../src/shared/preferences";
import { QueryClientProvider } from "@tanstack/react-query";
import { queryClient } from "../src/shared/api/queries";
import { Dialog } from "../src/shared/ui/Primitives";
import { SessionProvider, useSession } from "../src/features/auth/Session";
import LoginDialog from "../src/features/auth/LoginDialog";
import ProfilePage from "../src/features/social/ProfilePage";
function Wrapper({ children, entries = ["/"] }) {
  return (
    <MemoryRouter
      initialEntries={entries}
      future={{ v7_startTransition: true, v7_relativeSplatPath: true }}
    >
      <QueryClientProvider client={queryClient}>
        <SessionProvider>{children}</SessionProvider>
      </QueryClientProvider>
    </MemoryRouter>
  );
}
function SessionView() {
  const session = useSession();
  return (
    <>
      <span>{session.authenticated ? "authenticated" : "guest"}</span>
      {session.error && <span>profile error</span>}
      <button onClick={session.logout}>logout</button>
      {session.logoutError && <span>logout error</span>}
    </>
  );
}
const json = (data, status = 200) =>
  new Response(JSON.stringify(data), {
    status,
    headers: { "Content-Type": "application/json" },
  });
describe("session and accessible UI", () => {
  it("keeps a session after a profile 503, revokes it only after server logout", async () => {
    localStorage.setItem("token", "example");
    const fetch = vi
      .spyOn(globalThis, "fetch")
      .mockImplementation(async (url, options) =>
        url.endsWith("/auth/logout")
          ? json({ ok: true })
          : json({ error: "Busy" }, 503),
      );
    render(
      <Wrapper>
        <SessionView />
      </Wrapper>,
    );
    await screen.findByText("profile error");
    expect(localStorage.getItem("token")).toBe("example");
    fireEvent.click(screen.getByText("logout"));
    await screen.findByText("guest");
    expect(localStorage.getItem("token")).toBeNull();
    expect(
      fetch.mock.calls.some(
        ([url, options]) =>
          url.endsWith("/auth/logout") && options.method === "POST",
      ),
    ).toBe(true);
  });
  it("does not pretend logout succeeded when revocation is unavailable", async () => {
    localStorage.setItem("token", "example");
    vi.spyOn(globalThis, "fetch").mockImplementation(async (url) =>
      url.endsWith("/auth/logout")
        ? json({ error: "Busy" }, 503)
        : json({ user: { id: 1, username: "test" } }),
    );
    render(
      <Wrapper>
        <SessionView />
      </Wrapper>,
    );
    fireEvent.click(screen.getByText("logout"));
    await screen.findByText("logout error");
    expect(localStorage.getItem("token")).toBe("example");
  });
  it("clears revoked credentials", async () => {
    localStorage.setItem("token", "revoked");
    vi.spyOn(globalThis, "fetch").mockResolvedValue(
      json({ error: "Revoked" }, 401),
    );
    render(
      <Wrapper>
        <SessionView />
      </Wrapper>,
    );
    await screen.findByText("guest");
    expect(localStorage.getItem("token")).toBeNull();
  });
  it("retains a controlled dialog through StrictMode and supports cancel", async () => {
    function Example() {
      const [open, setOpen] = useState(true);
      return (
        <Dialog open={open} onClose={() => setOpen(false)} title="Языки">
          <input aria-label="test" />
        </Dialog>
      );
    }
    render(
      <StrictMode>
        <MemoryRouter>
          <Example />
        </MemoryRouter>
      </StrictMode>,
    );
    expect(screen.getByRole("dialog").open).toBe(true);
    fireEvent(
      screen.getByRole("dialog"),
      new Event("cancel", { cancelable: true }),
    );
    await waitFor(() => expect(screen.queryByRole("dialog")).toBeNull());
  });
  it("offers only Twitch OAuth and requires consent", () => {
    vi.spyOn(globalThis, "crypto", "get").mockReturnValue(webcrypto);
    render(
      <Wrapper>
        <LoginDialog
          open
          onClose={() => {}}
          config={{
            data: {
              legal: { configured: true, version: "test" },
              auth: { twitch_enabled: true },
            },
          }}
        />
      </Wrapper>,
    );
    expect(screen.queryByLabelText("Пароль")).toBeNull();
    expect(screen.queryByText("По почте")).toBeNull();
    expect(
      screen.getByRole("button", { name: /Продолжить с Twitch/ }).disabled,
    ).toBe(true);
    fireEvent.click(screen.getByRole("checkbox"));
    expect(
      screen.getByRole("button", { name: /Продолжить с Twitch/ }).disabled,
    ).toBe(false);
  });
  it("renders a single profile for a Twitch channel without a local account", async () => {
    vi.spyOn(globalThis, "fetch").mockImplementation(async (url) =>
      url.includes("/profiles/twitch/")
        ? json({
            profile: {
              key: "t-123",
              canonical_path: "/p/twitch/123",
              display_name: "Streamer",
              twitch: {
                id: "123",
                login: "streamer",
                display_name: "Streamer",
              },
              local_user: null,
              relationship: { following: false },
              capabilities: { can_view_profile: true },
              stats: { followers: 0, following: 0, friends: 0, clips: 0 },
            },
          })
        : json({ clips: [], has_more: false }),
    );
    render(
      <Wrapper entries={["/p/twitch/123"]}>
        <Routes>
          <Route
            element={
              <PreferencesProvider>
                <Outlet context={{ openLogin: () => {} }} />
              </PreferencesProvider>
            }
          >
            <Route
              path="/p/twitch/:id"
              element={<ProfilePage source="twitch" />}
            />
          </Route>
        </Routes>
      </Wrapper>,
    );
    await screen.findByRole("heading", { name: "Streamer" });
    expect(screen.queryByRole("button", { name: "Друзья" })).toBeNull();
    expect(screen.queryByText("Профиль в ClipStream")).toBeNull();
    expect(
      screen.getByRole("link", { name: /Открыть Twitch/ }).getAttribute("href"),
    ).toBe("https://www.twitch.tv/streamer");
    fireEvent.keyDown(screen.getByRole("combobox"), { key: "ArrowDown" });
    fireEvent.click(
      await screen.findByRole("option", { name: "С этого канала" }),
    );
    await waitFor(() =>
      expect(
        globalThis.fetch.mock.calls.some(
          ([url]) =>
            url.includes("/profiles/t-123/clips") &&
            url.includes("scope=channel"),
        ),
      ).toBe(true),
    );
  });
});
