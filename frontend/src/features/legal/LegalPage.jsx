import ExternalLink from "../../shared/ui/ExternalLink";
import { Surface } from "../../shared/ui/Layout";
import { Link, useOutletContext, useParams } from "react-router-dom";
import { ArrowUpRight, FileText } from "lucide-react";
import { PageHeading, Status } from "../../shared/ui/Primitives";
import { documents } from "./documents";

const titles = {
  privacy: "Политика конфиденциальности",
  terms: "Условия использования",
  consent: "Согласие на обработку данных",
  cookies: "Данные в браузере",
  offer: "Условия размещения рекламы",
};
export default function LegalPage() {
  const { slug } = useParams();
  const { config, configQuery } = useOutletContext();
  const legal = config?.legal;
  const document = documents[slug];
  const url = legal?.documents?.[slug];
  const externalUrl = /^https?:\/\//.test(url || "") ? url : null;
  return (
    <div className="page narrow">
      <PageHeading
        eyebrow="ClipStream"
        title={titles[slug] || "Документы и контакты"}
        description="Условия работы сервиса и связь с оператором."
      />
      {!legal ? (
        <Status
          loading={!configQuery.error}
          error={configQuery.error}
          onRetry={configQuery.refetch}
        />
      ) : (
        <>
          <Surface className="operator">
            <h2>
              {legal.operator_name || "Оператор: [указать ФИО или организацию]"}
            </h2>
            {legal.address && <p>{legal.address}</p>}
            {legal.contact_email ? (
              <a className="text-link" href={`mailto:${legal.contact_email}`}>
                {legal.contact_email}
              </a>
            ) : (
              <p>Контакт для обращений: [указать email]</p>
            )}
            <p className="muted">Версия документов: {legal.version}</p>
          </Surface>
          {slug ? (
            <>
              {document && externalUrl ? (
                <ExternalLink className="document-row" href={externalUrl}>
                  <FileText />
                  Открыть опубликованную редакцию <ArrowUpRight size={18} />
                </ExternalLink>
              ) : document ? (
                <Surface as="article" className="prose">
                  {document.sections.map(([title, text]) => (
                    <section key={title}>
                      <h2>{title}</h2>
                      <p>{text}</p>
                    </section>
                  ))}
                </Surface>
              ) : (
                <Status empty="Документ не найден" />
              )}
              <Link className="text-link" to="/legal">
                Все документы
              </Link>
            </>
          ) : (
            <div className="document-list">
              {Object.entries(titles).map(([key, title]) => (
                <Link key={key} to={`/legal/${key}`} className="document-row">
                  <FileText size={19} />
                  {title}
                  <ArrowUpRight size={17} />
                </Link>
              ))}
            </div>
          )}
        </>
      )}
    </div>
  );
}
