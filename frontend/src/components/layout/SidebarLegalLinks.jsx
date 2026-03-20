import { Link } from 'react-router-dom';
import { FileText } from 'lucide-react';
import { LEGAL_INDEX } from '../../data/legalDocuments';

/** Блок ссылок внизу гайда подписок (как на YouTube). */
export default function SidebarLegalLinks({ variant = 'expanded' }) {
  const year = new Date().getFullYear();

  if (variant === 'collapsed') {
    return (
      <div className="shrink-0 border-t border-brand-700/40 pt-2 pb-2 mt-auto flex justify-center px-0.5">
        <Link
          to="/legal"
          className="flex h-9 w-9 items-center justify-center rounded-lg text-gray-500 hover:text-brand-400 hover:bg-brand-800/70 transition-colors"
          title="Правовая информация и документы"
          aria-label="Документы и правовая информация"
        >
          <FileText className="w-4 h-4" strokeWidth={2} />
        </Link>
      </div>
    );
  }

  return (
    <div className="shrink-0 border-t border-brand-700/50 pt-3 pb-1 mt-auto">
      <nav
        className="flex flex-wrap gap-x-2.5 gap-y-1 text-[11px] leading-snug text-gray-500"
        aria-label="Юридическая информация"
      >
        <Link to="/legal" className="hover:text-brand-400 transition-colors">
          Все документы
        </Link>
        {LEGAL_INDEX.map((doc) => (
          <Link key={doc.slug} to={`/legal/${doc.slug}`} className="hover:text-brand-400 transition-colors">
            {doc.navShort}
          </Link>
        ))}
      </nav>
      <p className="text-[10px] text-gray-600 mt-2.5 leading-relaxed">
        © {year} ClipStream · Контент Twitch — правообладателей
      </p>
    </div>
  );
}
