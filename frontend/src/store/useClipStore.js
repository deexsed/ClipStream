import { create } from 'zustand';
import { clips as clipsApi } from '../api/client';

const useClipStore = create((set, get) => ({
  clips: [],
  loading: false,
  hasMore: true,
  offset: 0,
  filter: {
    game_id: '',
    sort: 'popular',
    period: 'all',
    tag: '',
    search: '',
  },

  setFilter: (patch) => {
    set((s) => ({
      filter: { ...s.filter, ...patch },
      clips: [],
      offset: 0,
      hasMore: true,
    }));
    get().fetchClips();
  },

  fetchClips: async () => {
    const { filter, offset, loading } = get();
    if (loading) return;

    set({ loading: true });
    try {
      let data;

      if (filter.search) {
        data = await clipsApi.search(filter.search, 20);
      } else {
        const params = new URLSearchParams();
        if (filter.game_id) params.set('game_id', filter.game_id);
        if (filter.sort)    params.set('sort', filter.sort);
        if (filter.period)  params.set('period', filter.period);
        if (filter.tag)     params.set('tag', filter.tag);
        params.set('limit', '20');
        params.set('offset', String(offset));
        data = await clipsApi.feed(params.toString());
      }

      const newClips = data.clips || [];

      set((s) => ({
        clips: offset === 0 ? newClips : [...s.clips, ...newClips],
        offset: s.offset + newClips.length,
        hasMore: !filter.search && newClips.length >= 20,
        loading: false,
      }));
    } catch {
      set({ loading: false });
    }
  },
}));

export default useClipStore;
