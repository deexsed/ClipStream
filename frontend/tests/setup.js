import { afterEach, vi } from "vitest";
import { cleanup } from "@testing-library/react";
import { queryClient } from "../src/shared/api/queries";
queryClient.setDefaultOptions({
  queries: { retry: false, gcTime: 0 },
  mutations: { retry: false },
});
Object.defineProperty(HTMLDialogElement.prototype, "showModal", {
  configurable: true,
  value() {
    this.open = true;
  },
});
Object.defineProperty(HTMLDialogElement.prototype, "close", {
  configurable: true,
  value() {
    this.open = false;
    this.dispatchEvent(new Event("close"));
  },
});
class ResizeObserver {
  observe() {}
  disconnect() {}
}
vi.stubGlobal("ResizeObserver", ResizeObserver);
afterEach(() => {
  cleanup();
  queryClient.clear();
  localStorage.clear();
  sessionStorage.clear();
  vi.restoreAllMocks();
  vi.useRealTimers();
});
