import { useEffect, useState } from 'react';
import { createPortal } from 'react-dom';
import { Link } from 'react-router-dom';
import { Play, X } from 'lucide-react';
import { LEGAL_CONSENT_VERSION, OAUTH_LEGAL_STORAGE_KEY } from '../../constants/legalConsent';
import { auth as authApi } from '../../api/client';

export default function TwitchLoginModal({ open, onClose, onStartedRedirect }) {
  const [pd, setPd] = useState(false);
  const [terms, setTerms] = useState(false);
  const [cookies, setCookies] = useState(false);
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState('');

  useEffect(() => {
    if (!open) {
      setPd(false);
      setTerms(false);
      setCookies(false);
      setErr('');
      setBusy(false);
    }
  }, [open]);

  useEffect(() => {
    if (!open) return;
    const prev = document.body.style.overflow;
    document.body.style.overflow = 'hidden';
    return () => {
      document.body.style.overflow = prev;
    };
  }, [open]);

  const allOk = pd && terms && cookies;

  const handleContinue = async () => {
    if (!allOk) return;
    setErr('');
    setBusy(true);
    try {
      sessionStorage.setItem(
        OAUTH_LEGAL_STORAGE_KEY,
        JSON.stringify({
          version: LEGAL_CONSENT_VERSION,
          accepted_at: new Date().toISOString(),
        })
      );
      const { url } = await authApi.twitchUrl();
      onStartedRedirect?.();
      window.location.href = url;
    } catch (e) {
      setErr(e.message || 'Не удалось начать вход');
      setBusy(false);
    }
  };

  if (!open) return null;

  return createPortal(
    <div
      className="fixed inset-0 z-[200] flex items-center justify-center p-4 sm:p-6 md:p-8"
      role="presentation"
      style={{ minHeight: '100dvh' }}
    >
      {/* Затемнение как у шапки сайта + размытие */}
      <button
        type="button"
        className="absolute inset-0 bg-brand-900/90 backdrop-blur-xl cursor-default"
        aria-hidden
        tabIndex={-1}
        onClick={onClose}
      />

      <div
        role="dialog"
        aria-modal="true"
        aria-labelledby="twitch-legal-title"
        className="relative z-10 flex w-full max-w-lg flex-col overflow-hidden rounded-2xl border border-brand-700/60 bg-brand-900/95 shadow-2xl shadow-black/50 ring-1 ring-brand-500/20 animate-slide-up max-h-[min(92dvh,44rem)]"
        onClick={(e) => e.stopPropagation()}
      >
        {/* Шапка в стиле Navbar: логотип + градиент */}
        <div className="flex shrink-0 items-start justify-between gap-4 border-b border-brand-700/60 bg-brand-900/85 px-5 py-4 sm:px-6 sm:py-5 backdrop-blur-xl">
          <div className="flex min-w-0 items-start gap-3">
            <div className="flex h-11 w-11 shrink-0 items-center justify-center rounded-xl bg-gradient-to-br from-brand-500 to-fuchsia-500 text-white shadow-lg shadow-brand-500/30">
              <Play className="h-5 w-5 fill-current" />
            </div>
            <div className="min-w-0 pt-0.5">
              <h2 id="twitch-legal-title" className="text-lg font-black tracking-tight text-white sm:text-xl">
                Вход через Twitch
              </h2>
              <p className="mt-1 text-sm leading-relaxed text-gray-400">
                Подтвердите документы — один раз в этом браузере или после их обновления. Версия:{' '}
                <span className="font-mono text-brand-400">{LEGAL_CONSENT_VERSION}</span>
              </p>
            </div>
          </div>
          <button
            type="button"
            onClick={onClose}
            className="shrink-0 rounded-xl p-2 text-gray-400 transition-colors hover:bg-brand-800/80 hover:text-white"
            aria-label="Закрыть"
          >
            <X className="h-5 w-5" strokeWidth={2.25} />
          </button>
        </div>

        <div className="min-h-0 flex-1 overflow-y-auto overscroll-contain px-5 py-5 sm:px-6 sm:py-6">
          <div className="space-y-3">
            <ConsentRow
              checked={pd}
              onChange={setPd}
              label={
                <>
                  Ознакомлен(а) с{' '}
                  <LegalLink to="/legal/privacy">Политикой в отношении обработки персональных данных</LegalLink> и даю{' '}
                  <LegalLink to="/legal/consent">согласие на обработку персональных данных</LegalLink>.
                </>
              }
            />
            <ConsentRow
              checked={terms}
              onChange={setTerms}
              label={
                <>
                  Принимаю <LegalLink to="/legal/terms">Пользовательское соглашение</LegalLink> и{' '}
                  <LegalLink to="/legal/offer">публичную оферту</LegalLink>.
                </>
              }
            />
            <ConsentRow
              checked={cookies}
              onChange={setCookies}
              label={
                <>
                  Ознакомлен(а) со <LegalLink to="/legal/cookies">сведениями об использовании cookie</LegalLink>.
                </>
              }
            />
          </div>

          {err && (
            <p
              className="mt-4 rounded-xl border border-red-500/35 bg-red-950/40 px-3 py-2.5 text-sm text-red-300"
              role="alert"
            >
              {err}
            </p>
          )}
        </div>

        <div className="flex shrink-0 flex-col gap-3 border-t border-brand-700/60 bg-brand-900/90 px-5 py-4 sm:flex-row sm:items-center sm:justify-end sm:px-6 sm:py-4 backdrop-blur-xl">
          <button
            type="button"
            onClick={onClose}
            className="order-2 w-full rounded-xl px-4 py-2.5 text-sm font-medium text-gray-300 transition-colors hover:bg-brand-800/80 hover:text-white sm:order-1 sm:w-auto"
          >
            Отмена
          </button>
          <button
            type="button"
            disabled={!allOk || busy}
            onClick={handleContinue}
            className="order-1 flex w-full items-center justify-center gap-2 rounded-xl bg-[#9146FF] px-5 py-2.5 text-sm font-bold text-white shadow-lg shadow-brand-500/25 transition-all hover:bg-[#772CE8] disabled:cursor-not-allowed disabled:opacity-45 disabled:shadow-none sm:order-2 sm:w-auto sm:min-w-[200px]"
          >
            <svg viewBox="0 0 24 24" className="h-4 w-4 shrink-0 fill-current" aria-hidden>
              <path d="M11.64 5.93H13.07V10.21H11.64M15.57 5.93H17V10.21H15.57M7 2L3.43 5.57V18.43H7.71V22L11.29 18.43H14.14L20.57 12V2M19.14 11.29L16.29 14.14H13.43L10.93 16.64V14.14H7.71V3.43H19.14Z" />
            </svg>
            {busy ? 'Переход к Twitch…' : 'Продолжить с Twitch'}
          </button>
        </div>
      </div>
    </div>,
    document.body
  );
}

function LegalLink({ to, children }) {
  return (
    <Link
      to={to}
      target="_blank"
      rel="noreferrer"
      className="font-medium text-brand-400 underline decoration-brand-500/50 underline-offset-[3px] transition-colors hover:text-brand-300 hover:decoration-brand-400"
      onClick={(e) => e.stopPropagation()}
    >
      {children}
    </Link>
  );
}

function ConsentRow({ checked, onChange, label }) {
  return (
    <label className="flex cursor-pointer gap-3 rounded-xl border border-brand-700/50 bg-brand-800/40 p-4 transition-colors hover:border-brand-600/60 hover:bg-brand-800/80">
      <input
        type="checkbox"
        checked={checked}
        onChange={(e) => onChange(e.target.checked)}
        className="mt-0.5 h-[1.125rem] w-[1.125rem] shrink-0 rounded border-2 border-brand-600 bg-brand-950 text-brand-500 focus:ring-2 focus:ring-brand-500/40 focus:ring-offset-0 focus:ring-offset-brand-900"
      />
      <span className="text-sm leading-relaxed text-gray-200">{label}</span>
    </label>
  );
}
