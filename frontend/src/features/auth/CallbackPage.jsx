import { useEffect, useRef, useState } from "react";
import { useNavigate } from "react-router-dom";
import { request } from "../../shared/api/http";
import { Status, ButtonLink } from "../../shared/ui/Primitives";
import { useSession } from "./Session";

const exchanges = new Map();
export default function CallbackPage() {
  const navigate = useNavigate();
  const session = useSession();
  const acceptRef = useRef(session.accept);
  acceptRef.current = session.accept;
  const [payload] = useState(() =>
    Object.fromEntries(new URLSearchParams(window.location.search)),
  );
  const [error, setError] = useState(null);
  useEffect(() => {
    let active = true;
    try {
      for (const [key, value] of exchanges) {
        if (value.expiresAt <= Date.now()) exchanges.delete(key);
      }
      const stored = JSON.parse(
        sessionStorage.getItem("clipstream.oauth") || "null",
      );
      // Query cleanup can remount the route. Retain the verified transaction in
      // memory until it settles, without putting the authorization code in storage.
      const pending = stored && exchanges.get(stored.state);
      const callback =
        !payload.code && !payload.state && !payload.error && pending
          ? pending.payload
          : payload;
      if (callback.error)
        throw new Error("Вход отменён в Twitch. Можно попробовать снова.");
      if (!callback.code || !callback.state) {
        if (session.loading) return;
        if (session.user) {
          navigate("/", { replace: true });
          return;
        }
        if (session.error) throw session.error;
        throw new Error("Нет данных авторизации. Начните вход заново.");
      }
      if (!stored || stored.state !== callback.state)
        throw new Error("Не удалось проверить запрос входа. Начните заново.");
      const key = callback.state;
      if (!exchanges.has(key)) {
        if (exchanges.size >= 4)
          exchanges.delete(exchanges.keys().next().value);
        exchanges.set(key, {
          payload: callback,
          verifier: stored.verifier,
          expiresAt: Date.now() + 300000,
          promise: request("/auth/twitch", {
            method: "POST",
            body: {
              code: callback.code,
              state: callback.state,
              verifier: stored.verifier,
              consent: stored.consent,
            },
          }),
        });
      }
      const exchange = exchanges.get(key);
      if (
        exchange.verifier !== stored.verifier ||
        exchange.payload.code !== callback.code
      )
        throw new Error("Не удалось проверить запрос входа. Начните заново.");
      navigate("/auth/callback", { replace: true });
      exchanges
        .get(key)
        .promise.then((result) => {
          if (!active) return;
          sessionStorage.removeItem("clipstream.oauth");
          acceptRef.current(result);
          exchanges.delete(key);
          navigate("/", { replace: true });
        })
        .catch((reason) => {
          if (active) {
            sessionStorage.removeItem("clipstream.oauth");
            exchanges.delete(key);
            setError(reason);
          }
        });
    } catch (reason) {
      setError(reason);
    }
    return () => {
      active = false;
    };
  }, [payload, navigate, session.loading, session.user, session.error]);
  return (
    <div className="page narrow">
      <Status loading={!error} error={error} />
      {error && <ButtonLink to="/">На главную</ButtonLink>}
    </div>
  );
}
