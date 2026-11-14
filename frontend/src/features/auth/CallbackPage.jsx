import { useEffect, useRef, useState } from "react";
import { Link, useNavigate } from "react-router-dom";
import { request } from "../../shared/api/http";
import { Status } from "../../shared/ui/Primitives";
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
    navigate("/auth/callback", { replace: true });
    try {
      if (payload.error)
        throw new Error("Вход отменён в Twitch. Можно попробовать снова.");
      if (!payload.code || !payload.state)
        throw new Error("Нет данных авторизации. Начните вход заново.");
      const stored = JSON.parse(
        sessionStorage.getItem("clipstream.oauth") || "null",
      );
      if (!stored || stored.state !== payload.state)
        throw new Error("Не удалось проверить запрос входа. Начните заново.");
      const key = payload.code;
      if (!exchanges.has(key)) {
        if (exchanges.size > 4) exchanges.clear();
        exchanges.set(
          key,
          request("/auth/twitch", {
            method: "POST",
            body: {
              code: payload.code,
              state: payload.state,
              verifier: stored.verifier,
              consent: stored.consent,
            },
          }),
        );
      }
      exchanges
        .get(key)
        .then((result) => {
          if (!active) return;
          sessionStorage.removeItem("clipstream.oauth");
          acceptRef.current(result);
          navigate("/", { replace: true });
        })
        .catch((reason) => {
          if (active) {
            sessionStorage.removeItem("clipstream.oauth");
            setError(reason);
          }
        });
    } catch (reason) {
      setError(reason);
    }
    return () => {
      active = false;
    };
  }, [payload, navigate]);
  return (
    <div className="page narrow">
      <Status loading={!error} error={error} />
      {error && (
        <Link to="/" className="button">
          На главную
        </Link>
      )}
    </div>
  );
}
