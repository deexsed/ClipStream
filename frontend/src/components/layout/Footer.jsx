import { Link } from 'react-router-dom';
import { LEGAL_INDEX } from '../../data/legalDocuments';

export default function Footer() {
  const year = new Date().getFullYear();

  return (
    <footer className="shrink-0 border-t border-brand-700/50 bg-brand-950/90 backdrop-blur-sm z-10 xl:hidden">
      <div className="max-w-7xl mx-auto px-4 py-4 lg:px-6">
        <div className="flex flex-col gap-3">
          <p className="text-xs text-gray-500">
            © {year} ClipStream. Контент Twitch принадлежит правообладателям.
          </p>
          <nav className="flex flex-wrap gap-x-3 gap-y-1.5 text-xs" aria-label="Юридическая информация">
            <Link to="/legal" className="text-gray-400 hover:text-brand-300 transition-colors">
              Документы
            </Link>
            {LEGAL_INDEX.map((doc) => (
              <Link
                key={doc.slug}
                to={`/legal/${doc.slug}`}
                className="text-gray-400 hover:text-brand-300 transition-colors"
              >
                {doc.navShort}
              </Link>
            ))}
          </nav>
        </div>
      </div>
    </footer>
  );
}
