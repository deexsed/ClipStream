import { ArrowUpRight } from "lucide-react";
import { Link } from "react-router-dom";
import { Dialog } from "../../shared/ui/Primitives";
import { Field, Input } from "../../shared/ui/FormControls";

export default function ShareDialog({
  open,
  onClose,
  clipId,
  title = "Ссылка на клип",
  label = "Скопируйте ссылку",
  showPageLink = false,
  className,
}) {
  return (
    <Dialog open={open} onClose={onClose} title={title} className={className}>
      <div className="form-stack">
        <Field>
          {label}
          <Input
            readOnly
            value={`${window.location.origin}/clip/${clipId}`}
            onFocus={(event) => event.target.select()}
          />
        </Field>
        {showPageLink && (
          <Link className="text-link" to={`/clip/${clipId}`}>
            Открыть страницу клипа <ArrowUpRight size={16} />
          </Link>
        )}
      </div>
    </Dialog>
  );
}
