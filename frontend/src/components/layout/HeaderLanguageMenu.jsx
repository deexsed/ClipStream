import { useEffect, useRef, useState } from 'react';
import { ChevronDown, Check } from 'lucide-react';
import { TWITCH_BROADCAST_LANGUAGES } from '../../data/twitchLanguages';
import { useClipLangStore } from '../../store/useClipLangStore';

export default function HeaderLanguageMenu() {
  const [open, setOpen] = useState(false);
  const rootRef = useRef(null);
  const codes = useClipLangStore((s) => s.codes);
  const toggleCode = useClipLangStore((s) => s.toggleCode);
  const clearCodes = useClipLangStore((s) => s.clearCodes);
  const count = codes.length;

  useEffect(() => {
    if (!open) return;
    const onDoc = (e) => {
      if (rootRef.current && !rootRef.current.contains(e.target)) setOpen(false);
    };
    document.addEventListener('mousedown', onDoc);
    return () => document.removeEventListener('mousedown', onDoc);
  }, [open]);

  return (
    <div className="relative shrink-0" ref={rootRef}>
      <button
        type="button"
        onClick={() => setOpen((v) => !v)}
        className="flex items-center gap-2 h-9 pl-3 pr-2 rounded-xl border border-white/20 bg-brand-900/80 hover:bg-brand-800/90 text-white text-sm transition-colors"
        aria-expanded={open}
        aria-haspopup="listbox"
      >
        <span className="hidden sm:inline font-medium">Язык клипов</span>
        {count > 0 && (
          <span className="min-w-[1.25rem] h-5 px-1 flex items-center justify-center rounded-md bg-[#9146FF] text-[11px] font-bold">
            {count}
          </span>
        )}
        <ChevronDown className={`w-4 h-4 text-gray-400 transition-transform ${open ? 'rotate-180' : ''}`} />
      </button>

      {open && (
        <div
          className="absolute right-0 top-full mt-2 w-[min(100vw-2rem,18rem)] max-h-[min(70vh,22rem)] flex flex-col rounded-xl border border-brand-600/80 bg-[#18181b] shadow-xl shadow-black/50 z-50 overflow-hidden"
          role="listbox"
        >
          <p className="px-3 py-2.5 text-[11px] text-gray-500 leading-snug border-b border-brand-700/50 shrink-0">
            Показываем клипы на выбранных языках вещания — так лента и поиск ближе к тому, что вы реально смотрите.
          </p>
          <div className="overflow-y-auto overscroll-contain flex-1 py-1">
            {TWITCH_BROADCAST_LANGUAGES.map(({ code, label }) => {
              const on = codes.includes(code);
              return (
                <button
                  key={code}
                  type="button"
                  role="option"
                  aria-selected={on}
                  onClick={() => toggleCode(code)}
                  className={`w-full flex items-center gap-3 px-3 py-2.5 text-left text-sm transition-colors ${
                    on ? 'bg-brand-800/70 ring-1 ring-inset ring-white/25' : 'hover:bg-brand-800/50 text-gray-200'
                  }`}
                >
                  <span
                    className={`w-4 h-4 rounded border shrink-0 flex items-center justify-center ${
                      on ? 'bg-[#9146FF] border-[#9146FF]' : 'border-brand-500/60 bg-brand-950/50'
                    }`}
                  >
                    {on ? <Check className="w-3 h-3 text-white" strokeWidth={3} /> : null}
                  </span>
                  <span className="truncate">{label}</span>
                </button>
              );
            })}
          </div>
          <div className="border-t border-brand-700/60 p-2 shrink-0">
            <button
              type="button"
              onClick={() => clearCodes()}
              className="w-full py-2 text-sm font-semibold text-[#bf94ff] hover:text-[#d4b3ff] hover:bg-brand-800/40 rounded-lg transition-colors"
            >
              Очистить все
            </button>
          </div>
        </div>
      )}
    </div>
  );
}
