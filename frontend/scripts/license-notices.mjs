import { readFileSync, readdirSync } from "node:fs";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
const root = fileURLToPath(new URL("../", import.meta.url));
export default function licenseNotices() {
  return {
    name: "third-party-notices",
    generateBundle() {
      const lock = JSON.parse(
        readFileSync(resolve(root, "package-lock.json"), "utf8"),
      );
      const notices = [
        "ClipStream — bundled third-party notices",
        "These notices cover production JavaScript packages and the bundled Inter font.",
        "",
      ];
      for (const [path, entry] of Object.entries(lock.packages || {})) {
        if (!path.startsWith("node_modules/") || entry.dev || entry.devOptional)
          continue;
        const directory = resolve(root, path);
        const pkg = JSON.parse(
          readFileSync(resolve(directory, "package.json"), "utf8"),
        );
        const files = readdirSync(directory).filter((name) =>
          /^(license|licence|copying|ofl)(?:[.-].*)?$/i.test(name),
        );
        if (!files.length)
          throw Error("Missing distributed license notice: " + pkg.name);
        notices.push("=".repeat(72), `${pkg.name} ${pkg.version}`, "");
        for (const file of files)
          notices.push(readFileSync(resolve(directory, file), "utf8"), "");
      }
      this.emitFile({
        type: "asset",
        fileName: "third-party-notices.txt",
        source: notices.join("\n"),
      });
    },
  };
}
