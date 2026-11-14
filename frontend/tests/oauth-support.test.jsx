import React from "react";
import { afterEach, expect, it, vi } from "vitest";
import { webcrypto } from "node:crypto";
import { render, screen, fireEvent } from "@testing-library/react";
import { MemoryRouter } from "react-router-dom";
import { QueryClientProvider } from "@tanstack/react-query";
import { queryClient } from "../src/shared/api/queries";
import {
  beginOAuth,
  oauthUnavailableReason,
} from "../src/features/auth/Session";
import LoginDialog from "../src/features/auth/LoginDialog";

afterEach(() => vi.unstubAllGlobals());

it("explains why HTTP LAN sign-in is unavailable before requesting OAuth", async () => {
  vi.stubGlobal("isSecureContext", false);
  vi.stubGlobal("crypto", {
    getRandomValues: webcrypto.getRandomValues.bind(webcrypto),
  });
  const fetch = vi.spyOn(globalThis, "fetch");
  render(
    <MemoryRouter>
      <QueryClientProvider client={queryClient}>
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
      </QueryClientProvider>
    </MemoryRouter>,
  );
  expect(screen.getByRole("status").textContent).toContain("HTTPS");
  fireEvent.click(screen.getByRole("checkbox"));
  expect(
    screen.getByRole("button", { name: /Продолжить с Twitch/ }).disabled,
  ).toBe(true);
  await expect(beginOAuth({ version: "test" })).rejects.toThrow(
    "HTTP-адресу локальной сети",
  );
  expect(fetch).not.toHaveBeenCalled();
  expect(sessionStorage.getItem("clipstream.oauth")).toBeNull();
});

it("reports missing Web Crypto without a raw JavaScript exception", async () => {
  vi.stubGlobal("isSecureContext", true);
  vi.stubGlobal("crypto", {});
  await expect(beginOAuth({})).rejects.toThrow(
    "Браузер не поддерживает безопасный вход",
  );
});

it("accepts secure contexts with the required cryptographic APIs", () => {
  vi.stubGlobal("isSecureContext", true);
  vi.stubGlobal("crypto", webcrypto);
  expect(oauthUnavailableReason()).toBeNull();
});
