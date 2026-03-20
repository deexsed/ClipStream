import { Link, useParams } from 'react-router-dom';
import { FileText, ArrowLeft } from 'lucide-react';
import { LEGAL_DOCUMENTS, LEGAL_INDEX, OPERATOR_PLACEHOLDER } from '../data/legalDocuments';

function SectionBlock({ block }) {
  if (block.type === 'h2') {
    return <h2 className="text-lg font-bold text-white mt-8 mb-3 first:mt-0">{block.title}</h2>;
  }
  if (block.type === 'p') {
    return <p className="text-sm text-gray-300 leading-relaxed mb-4">{block.text}</p>;
  }
  if (block.type === 'ul') {
    return (
      <ul className="list-disc pl-5 space-y-2 text-sm text-gray-300 mb-4">
        {block.items.map((item, i) => (
          <li key={i} className="leading-relaxed">
            {item}
          </li>
        ))}
      </ul>
    );
  }
  return null;
}

function LegalHub() {
  return (
    <div className="max-w-3xl mx-auto px-4 py-10 lg:py-14">
      <h1 className="text-2xl lg:text-3xl font-black text-white mb-2">Документы и политики</h1>
      <p className="text-sm text-gray-400 mb-8 max-w-2xl">
        Оператор и исполнитель по оферте — вы как <strong className="text-gray-300">физическое лицо</strong>
        (не организация). Документы согласованы с ФЗ № 152-ФЗ, ГК РФ, ЗоЗПП и ФЗ № 149-ФЗ. Подставьте в
        коде и на странице контактов своё ФИО, адрес для корреспонденции, e-mail; ИНН — если есть; при
        статусе самозанятого (НПД) укажите это отдельно. При сомнениях проконсультируйтесь с юристом.
      </p>
      <div className="rounded-2xl border border-brand-700/50 bg-brand-900/40 p-4 mb-8 text-xs text-gray-400 space-y-2">
        <p className="font-semibold text-gray-300">Реквизиты для подстановки (файл legalDocuments.js)</p>
        <p>
          ФИО: <span className="text-brand-400">{OPERATOR_PLACEHOLDER.fullName}</span>
        </p>
        <p>
          E-mail по ПДн: <span className="text-brand-400">{OPERATOR_PLACEHOLDER.email}</span>
        </p>
        <p className="text-gray-500 pt-1">{OPERATOR_PLACEHOLDER.activityNote}</p>
      </div>
      <ul className="space-y-3">
        {LEGAL_INDEX.map((doc) => (
          <li key={doc.slug}>
            <Link
              to={`/legal/${doc.slug}`}
              className="flex gap-4 items-start p-4 rounded-xl border border-brand-700/40 bg-brand-950/40 hover:border-brand-500/40 hover:bg-brand-900/50 transition-colors group"
            >
              <FileText className="w-5 h-5 text-brand-400 shrink-0 mt-0.5 group-hover:text-brand-300" />
              <div>
                <span className="text-white font-semibold group-hover:text-brand-200 transition-colors">
                  {doc.title}
                </span>
                <p className="text-sm text-gray-500 mt-1">{doc.short}</p>
              </div>
            </Link>
          </li>
        ))}
      </ul>
    </div>
  );
}

function LegalDocumentView({ slug }) {
  const doc = LEGAL_DOCUMENTS[slug];

  if (!doc) {
    return (
      <div className="max-w-3xl mx-auto px-4 py-16 text-center">
        <p className="text-gray-400 mb-6">Документ не найден.</p>
        <Link to="/legal" className="text-brand-400 hover:underline">
          К списку документов
        </Link>
      </div>
    );
  }

  return (
    <article className="max-w-3xl mx-auto px-4 py-10 lg:py-14 pb-24">
      <Link
        to="/legal"
        className="inline-flex items-center gap-2 text-sm text-gray-400 hover:text-white mb-8 transition-colors"
      >
        <ArrowLeft className="w-4 h-4" />
        Все документы
      </Link>
      <header className="mb-8 border-b border-brand-700/40 pb-6">
        <p className="text-xs font-bold uppercase tracking-wider text-brand-400 mb-2">ClipStream</p>
        <h1 className="text-2xl lg:text-3xl font-black text-white leading-tight">{doc.title}</h1>
        <p className="text-xs text-gray-500 mt-3">Редакция от {doc.updated}</p>
      </header>
      {doc.intro && <p className="text-sm text-gray-300 leading-relaxed mb-6">{doc.intro}</p>}
      <div className="prose prose-invert max-w-none">
        {doc.sections.map((block, i) => (
          <SectionBlock key={i} block={block} />
        ))}
      </div>
    </article>
  );
}

export default function LegalPage() {
  const { slug } = useParams();

  if (!slug) {
    return <LegalHub />;
  }

  return <LegalDocumentView slug={slug} />;
}
