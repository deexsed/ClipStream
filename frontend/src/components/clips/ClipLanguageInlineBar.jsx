import { Check } from 'lucide-react';
import { useSearchParams } from 'react-router-dom';
import { TWITCH_BROADCAST_LANGUAGES } from '../../data/twitchLanguages';
import { useClipLangStore } from '../../store/useClipLangStore';

export default function ClipLanguageInlineBar({ syncUrl = false }) {
  const [, setSearchParams] = useSearchParams();
  const codes = useClipLangStore((s) => s.codes);
  const toggleCode = useClipLangStore((s) => s.toggleCode);
  const clearCodes = useClipLangStore((s) => s.clearCodes);

  const pushLangsToUrl = (nextCodes) => {
    if (!syncUrl) return;
    setSearchParams(
      (prev) => {
        const n = new URLSearchParams(prev);
        if (nextCodes.length) n.set('langs', [...nextCodes].sort().join(','));
        else n.delete('langs');
        return n;
      },
      { replace: true },
    );
  };

  const onToggle = (code) => {
    toggleCode(code);
    if (syncUrl) pushLangsToUrl(useClipLangStore.getState().codes);
  };

  const onClear = () => {
    clearCodes();
    if (syncUrl) pushLangsToUrl([]);
  };

  return (
    <div className="space-y-3">
      <p className="text-[11px] text-gray-500 leading-snug max-w-2xl">
        Языки вещания клипов: отметьте те, на которых вам комфортно смотреть — выдача на этой странице подстроится.
      </p>
      <div className="flex flex-wrap items-center gap-2">
        {TWITCH_BROADCAST_LANGUAGES.map(({ code, label }) => {
          const on = codes.includes(code);
          const show = code === 'other' ? 'OTHER' : code.toUpperCase();
          return (
            <button
              key={code}
              type="button"
              title={label}
              aria-label={`${label}, код ${show}`}
              onClick={() => onToggle(code)}
              className={`inline-flex items-center gap-1.5 rounded-lg border px-2 py-1.5 text-xs font-semibold transition-colors ${
                on
                  ? 'border-[#9146FF]/80 bg-[#9146FF]/20 text-white'
                  : 'border-white/10 bg-white/[0.03] text-gray-500 hover:border-white/20 hover:text-gray-300'
              }`}
              aria-pressed={on}
            >
              <span
                className={`flex h-3.5 w-3.5 shrink-0 items-center justify-center rounded border ${
                  on ? 'border-[#9146FF] bg-[#9146FF]' : 'border-gray-600 bg-black/30'
                }`}
              >
                {on ? <Check className="h-2.5 w-2.5 text-white" strokeWidth={3} /> : null}
              </span>
              {show}
            </button>
          );
        })}
      </div>
      <button
        type="button"
        onClick={onClear}
        className="text-xs font-semibold text-[#bf94ff] hover:text-[#d4b3ff] transition-colors"
      >
        Сбросить языки (все клипы)
      </button>
    </div>
  );
}
