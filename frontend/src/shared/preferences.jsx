import { createContext, useContext, useEffect, useState } from "react";
import { useSearchParams } from "react-router-dom";
import { TWITCH_BROADCAST_LANGUAGES } from "../data/twitchLanguages";

const Context = createContext(null);
const allowed = new Set(TWITCH_BROADCAST_LANGUAGES.map((item) => item.code));
export function normalizeLanguages(value) {
  return [
    ...new Set(
      (Array.isArray(value) ? value : []).filter((code) => allowed.has(code)),
    ),
  ].sort();
}
export function PreferencesProvider({ children }) {
  const [params, setParams] = useSearchParams();
  const [stored, setStored] = useState(() => {
    try {
      return normalizeLanguages(
        JSON.parse(localStorage.getItem("clipstream.languages") || "[]"),
      );
    } catch {
      return [];
    }
  });
  const hasUrl = params.has("langs");
  const languages = hasUrl
    ? normalizeLanguages(params.get("langs").split(","))
    : stored;
  const signature = languages.join(",");
  useEffect(() => {
    try {
      localStorage.setItem(
        "clipstream.languages",
        JSON.stringify(signature ? signature.split(",") : []),
      );
    } catch {
      /* preference is still usable without persistence */
    }
  }, [signature]);
  const change = (next) => {
    const normalized = normalizeLanguages(next);
    setStored(normalized);
    setParams(
      (previous) => {
        const updated = new URLSearchParams(previous);
        updated.set("langs", normalized.join(","));
        return updated;
      },
      { replace: true },
    );
  };
  return (
    <Context.Provider
      value={{
        languages,
        languageKey: signature,
        toggleLanguage: (code) =>
          change(
            languages.includes(code)
              ? languages.filter((x) => x !== code)
              : [...languages, code],
          ),
        resetLanguages: () => change([]),
      }}
    >
      {children}
    </Context.Provider>
  );
}
export const usePreferences = () => useContext(Context);
