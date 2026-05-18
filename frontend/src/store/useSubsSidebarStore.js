import { create } from 'zustand';

const COLLAPSE_KEY = 'clipstream_subs_sidebar_collapsed';

function readCollapsed() {
  try {
    return localStorage.getItem(COLLAPSE_KEY) === 'true';
  } catch {
    return false;
  }
}

export const useSubsSidebarStore = create((set) => ({
  dockCollapsed: readCollapsed(),
  clipOverlayOpen: false,

  toggleDockCollapsed: () =>
    set((s) => {
      const next = !s.dockCollapsed;
      try {
        localStorage.setItem(COLLAPSE_KEY, String(next));
      } catch {
        /* ignore */
      }
      return { dockCollapsed: next };
    }),

  setDockCollapsed: (collapsed) =>
    set(() => {
      try {
        localStorage.setItem(COLLAPSE_KEY, String(collapsed));
      } catch {
        /* ignore */
      }
      return { dockCollapsed: collapsed };
    }),

  setClipOverlayOpen: (open) => set({ clipOverlayOpen: !!open }),

  toggleClipOverlay: () => set((s) => ({ clipOverlayOpen: !s.clipOverlayOpen })),
}));
