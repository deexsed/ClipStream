import useClipStore from '../../store/useClipStore';
import GameFilter from '../filters/GameFilter';
import TimeFilter from '../filters/TimeFilter';
import SortFilter from '../filters/SortFilter';
import TagFilter from '../filters/TagFilter';

export default function Sidebar() {
  const { filter, setFilter } = useClipStore();

  return (
    <aside className="w-64 bg-brand-900 border-r border-brand-700 flex flex-col overflow-y-auto hidden lg:flex shrink-0">
      <div className="p-6">
        <h3 className="text-xs font-bold text-gray-500 uppercase tracking-wider mb-4">
          Фильтры
        </h3>

        <GameFilter value={filter.game_id} onChange={(v) => setFilter({ game_id: v })} />
        <TimeFilter value={filter.period} onChange={(v) => setFilter({ period: v })} />
        <SortFilter value={filter.sort} onChange={(v) => setFilter({ sort: v })} />
        <TagFilter value={filter.tag} onChange={(v) => setFilter({ tag: v })} />
      </div>

      <div className="mt-auto p-6 border-t border-brand-700">
        <div className="bg-gradient-to-br from-brand-800 to-brand-900 rounded-xl p-4 border border-brand-700/50">
          <p className="text-xs text-gray-400 mb-2">Хочешь свой клип здесь?</p>
          <button className="w-full py-2 bg-white text-brand-900 text-xs font-bold rounded-lg hover:bg-gray-100 transition-colors">
            Предложить клип
          </button>
        </div>
      </div>
    </aside>
  );
}
