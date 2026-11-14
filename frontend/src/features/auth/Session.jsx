import { createContext, useContext, useEffect, useState } from "react";
import { useQuery } from "@tanstack/react-query";
import { request, readToken, saveToken } from "../../shared/api/http";
import { queryClient } from "../../shared/api/queries";

const SessionContext = createContext(null);
export function SessionProvider({ children }) {
  const [token, setToken] = useState(readToken);
  const profile = useQuery({
    queryKey: ["session", token],
    queryFn: ({ signal }) => request("/auth/me", { signal }),
    enabled: Boolean(token),
    staleTime: 60000,
  });
  const [logoutError, setLogoutError] = useState(null);
  const [loggingOut, setLoggingOut] = useState(false);
  const logout = async () => {
    if (loggingOut) return;
    setLoggingOut(true);
    setLogoutError(null);
    try {
      await request("/auth/logout", { method: "POST" });
      saveToken(null);
      setToken(null);
      queryClient.clear();
    } catch (error) {
      if ([401, 403].includes(error.status)) {
        saveToken(null);
        setToken(null);
        queryClient.clear();
      } else setLogoutError(error);
    } finally {
      setLoggingOut(false);
    }
  };
  useEffect(() => {
    if ([401, 403].includes(profile.error?.status)) {
      saveToken(null);
      setToken(null);
      queryClient.clear();
    }
  }, [profile.error]);
  useEffect(() => {
    const onStorage = (event) => {
      if (event.key === "token") {
        setToken(readToken());
        queryClient.clear();
      }
    };
    window.addEventListener("storage", onStorage);
    return () => window.removeEventListener("storage", onStorage);
  }, []);
  const accept = (result) => {
    saveToken(result.token);
    queryClient.clear();
    setToken(result.token);
    queryClient.setQueryData(["session", result.token], { user: result.user });
  };
  return (
    <SessionContext.Provider
      value={{
        user: profile.data?.user,
        profile: profile.data,
        authenticated: Boolean(token),
        loading: Boolean(token) && profile.isPending,
        error: profile.error,
        refresh: profile.refetch,
        accept,
        logout,
        logoutError,
        loggingOut,
      }}
    >
      {children}
    </SessionContext.Provider>
  );
}
export const useSession = () => useContext(SessionContext);

export async function beginOAuth(consent) {
  const verifier = Array.from(crypto.getRandomValues(new Uint8Array(32)), (x) =>
    x.toString(16).padStart(2, "0"),
  ).join("");
  const hash = await crypto.subtle.digest(
    "SHA-256",
    new TextEncoder().encode(verifier),
  );
  const challenge = Array.from(new Uint8Array(hash), (x) =>
    x.toString(16).padStart(2, "0"),
  ).join("");
  const data = await request(`/auth/twitch/url?challenge=${challenge}`);
  sessionStorage.setItem(
    "clipstream.oauth",
    JSON.stringify({ verifier, consent, state: data.state }),
  );
  const url = new URL(data.url);
  if (url.origin !== "https://id.twitch.tv")
    throw new Error("Некорректный адрес входа Twitch.");
  window.location.assign(url.href);
}
