import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import licenseNotices from "./scripts/license-notices.mjs";

export default defineConfig({
  base: process.env.ASSET_BASE_URL || "/",
  plugins: [react(), licenseNotices()],
  build: { outDir: "build", emptyOutDir: true },
  test: { environment: "jsdom", setupFiles: ["./tests/setup.js"] },
  server: {
    port: 3000,
    host: "127.0.0.1",
    proxy: {
      "/api": {
        target: process.env.API_PROXY_TARGET || "http://127.0.0.1:80",
        changeOrigin: true,
      },
    },
  },
});
