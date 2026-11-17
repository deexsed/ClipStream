import { useEffect, useState } from "react";
import { Link, Outlet, useOutletContext } from "react-router-dom";
import brandMark from "../assets/clipstream-raccoon.svg?no-inline";
import "./SiteFooter.css";

export function PublicPageLayout() {
  const context = useOutletContext();

  return (
    <div className="public-page-layout">
      <Outlet context={context} />
      <SiteFooter />
    </div>
  );
}

function SiteFooter() {
  const [year, setYear] = useState(() => new Date().getFullYear());

  useEffect(() => {
    let timer;

    function updateYear() {
      window.clearTimeout(timer);
      const now = new Date();
      const currentYear = now.getFullYear();
      setYear(currentYear);

      // Browsers limit setTimeout to a signed 32-bit delay.
      const nextYear = new Date(currentYear + 1, 0, 1);
      timer = window.setTimeout(
        updateYear,
        Math.min(nextYear - now, 2_147_483_647),
      );
    }

    function updateWhenVisible() {
      if (document.visibilityState === "visible") updateYear();
    }

    updateYear();
    document.addEventListener("visibilitychange", updateWhenVisible);

    return () => {
      window.clearTimeout(timer);
      document.removeEventListener("visibilitychange", updateWhenVisible);
    };
  }, []);

  return (
    <footer className="site-footer" role="contentinfo">
      <div className="site-footer-brand">
        <img src={brandMark} alt="" width="20" height="20" />
        <span>ClipStream</span>
        <small>© {year}</small>
      </div>
      <nav className="site-footer-links" aria-label="Документы и контакты">
        <Link to="/legal/terms">Условия</Link>
        <Link to="/legal/privacy">Конфиденциальность</Link>
        <Link to="/legal">Документы</Link>
      </nav>
    </footer>
  );
}
