import { describe, it, expect, vi } from "vitest";
import { request, queryString, readToken } from "../src/shared/api/http";
describe("API transport", () => {
  it("encodes multilingual filters without losing commas or plus characters", () => {
    const params = new URLSearchParams(
      queryString({
        q: "кино + игры",
        clip_langs: "ru,en",
        empty: "",
        omit: undefined,
      }),
    );
    expect(params.get("q")).toBe("кино + игры");
    expect(params.get("clip_langs")).toBe("ru,en");
    expect(params.has("omit")).toBe(false);
  });
  it("preserves authentication on service failure and exposes Retry-After", async () => {
    localStorage.setItem("token", "test-session");
    vi.spyOn(globalThis, "fetch").mockResolvedValue(
      new Response(JSON.stringify({ error: "Busy" }), {
        status: 503,
        headers: { "Retry-After": "12" },
      }),
    );
    await expect(request("/clips")).rejects.toMatchObject({
      status: 503,
      retryAfter: 12,
    });
    expect(readToken()).toBe("test-session");
  });
  it("propagates cancellation to in-flight requests", async () => {
    const controller = new AbortController();
    let received;
    vi.spyOn(globalThis, "fetch").mockImplementation((url, options) => {
      received = options.signal;
      return new Promise((resolve, reject) =>
        options.signal.addEventListener("abort", () =>
          reject(new DOMException("Aborted", "AbortError")),
        ),
      );
    });
    const pending = request("/clips", { signal: controller.signal });
    controller.abort();
    await expect(pending).rejects.toMatchObject({ name: "AbortError" });
    expect(received.aborted).toBe(true);
  });
  it("stops hung requests and returns a retryable timeout", async () => {
    vi.useFakeTimers();
    vi.spyOn(globalThis, "fetch").mockImplementation(
      (url, options) =>
        new Promise((resolve, reject) =>
          options.signal.addEventListener("abort", () =>
            reject(new DOMException("Aborted", "AbortError")),
          ),
        ),
    );
    const pending = request("/clips");
    const assertion = expect(pending).rejects.toMatchObject({ status: 0 });
    await vi.advanceTimersByTimeAsync(20000);
    await assertion;
  });
  it("rejects malformed successful payloads", async () => {
    vi.spyOn(globalThis, "fetch").mockResolvedValue(
      new Response("<html>broken</html>"),
    );
    await expect(request("/clips")).rejects.toMatchObject({ status: 200 });
  });
});
