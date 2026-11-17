import { ButtonLink } from "../../shared/ui/Primitives";
import { ArrowRight, Flame, House } from "lucide-react";

import "./NotFoundPage.css";

export default function NotFoundPage() {
  return (
    <section className="not-found" aria-labelledby="not-found-title">
      <div className="not-found-art" aria-hidden="true">
        <div className="not-found-frame">
          <div className="not-found-frame-label">
            <span>CLIPSTREAM</span>
            <span>СИГНАЛ ПОТЕРЯН</span>
          </div>
          <div className="not-found-code">
            <span>4</span>
            <span className="not-found-zero">
              <svg viewBox="0 0 48 48" fill="none">
                <path d="m19 14 16 10-16 10V14Z" fill="currentColor" />
              </svg>
            </span>
            <span>4</span>
          </div>
          <div className="not-found-timeline">
            <span className="not-found-track" />
            <span>--:--</span>
          </div>
        </div>
        <span className="not-found-caption">ЭТОТ МОМЕНТ ОСТАЛСЯ ЗА КАДРОМ</span>
      </div>

      <div className="not-found-copy">
        <p className="not-found-label">ОШИБКА 404</p>
        <h1 id="not-found-title">Кажется, мы сбились с эфира</h1>
        <p className="not-found-description">
          Такой страницы нет. Возможно, ссылка устарела или в адресе опечатка.
          Но хорошие моменты здесь — совсем рядом.
        </p>
        <div className="not-found-actions">
          <ButtonLink variant="primary" to="/">
            <House size={17} aria-hidden="true" />
            На главную
            <ArrowRight size={17} aria-hidden="true" />
          </ButtonLink>
          <ButtonLink to="/trends">
            <Flame size={17} aria-hidden="true" />
            Смотреть тренды
          </ButtonLink>
        </div>
      </div>
      <p className="not-found-footer">
        Ссылка потерялась. Хорошие клипы — нет.
      </p>
    </section>
  );
}
