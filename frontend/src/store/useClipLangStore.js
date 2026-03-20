import { create } from 'zustand';
import { persist } from 'zustand/middleware';

export const useClipLangStore = create(
  persist(
    (set, get) => ({
      /** Пустой массив — все языки (в query уходит clip_langs=). */
      codes: [],
      setCodes: (codes) => set({ codes: Array.isArray(codes) ? [...codes] : [] }),
      toggleCode: (code) =>
        set((s) => {
          const next = new Set(s.codes);
          if (next.has(code)) next.delete(code);
          else next.add(code);
          return { codes: Array.from(next) };
        }),
      clearCodes: () => set({ codes: [] }),
    }),
    { name: 'clipstream-clip-langs', partialize: (s) => ({ codes: s.codes }) },
  ),
);
