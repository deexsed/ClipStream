import { Checkbox } from "../../shared/ui/FormControls";
import { useEffect, useState } from "react";
import { Link } from "react-router-dom";
import { useMutation } from "@tanstack/react-query";
import { ArrowUpRight } from "lucide-react";
import { Button, Dialog, Status } from "../../shared/ui/Primitives";
import { beginOAuth, oauthUnavailableReason } from "./Session";

export default function LoginDialog({ open, onClose, config }) {
  const [consent, setConsent] = useState(false);
  const legal = config.data?.legal;
  const unavailable = oauthUnavailableReason();
  const mutation = useMutation({
    mutationFn: () =>
      beginOAuth({
        version: legal?.version,
        accepted_at: new Date().toISOString(),
      }),
  });
  useEffect(() => {
    if (!open) {
      setConsent(false);
      mutation.reset();
    }
  }, [open]);
  return (
    <Dialog open={open} onClose={onClose} title="Ваш Twitch. Ваши моменты.">
      <p className="muted">
        Один аккаунт для клипов, любимых каналов и общения. Вход — только через
        Twitch.
      </p>
      <Status
        loading={config.isPending}
        error={config.error}
        onRetry={config.refetch}
      >
        <form
          className="form-stack"
          onSubmit={(event) => {
            event.preventDefault();
            mutation.mutate();
          }}
        >
          <Checkbox
            checked={consent}
            onChange={(event) => setConsent(event.target.checked)}
            required
          >
            <span>
              Принимаю{" "}
              <Link to="/legal/terms" onClick={onClose}>
                условия сервиса
              </Link>{" "}
              и{" "}
              <Link to="/legal/consent" onClick={onClose}>
                согласие на обработку данных
              </Link>
              , ознакомился с{" "}
              <Link to="/legal/privacy" onClick={onClose}>
                политикой конфиденциальности
              </Link>
              .
            </span>
          </Checkbox>
          {!config.data?.auth?.twitch_enabled && (
            <p className="muted">Вход временно недоступен. Попробуйте позже.</p>
          )}
          {unavailable && <p role="status">{unavailable}</p>}
          <Button
            variant="primary"
            busy={mutation.isPending}
            disabled={
              !legal?.configured ||
              !consent ||
              Boolean(unavailable) ||
              !config.data?.auth?.twitch_enabled
            }
          >
            Продолжить с Twitch <ArrowUpRight size={17} />
          </Button>
          {mutation.error && <Status error={mutation.error} />}
          <small>
            Пароль Twitch вводится только на twitch.tv. ClipStream его не
            получает.
          </small>
        </form>
      </Status>
    </Dialog>
  );
}
