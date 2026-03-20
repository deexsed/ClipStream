const OPTIONS = [
  { value: 'popular',  label: 'Популярные' },
  { value: 'recent',   label: 'Новые' },
  { value: 'duration', label: 'Длинные' },
];

export default function SortFilter({ value, onChange }) {
  return (
    <div className="mb-6">
      <label className="block text-sm font-medium text-gray-300 mb-2">Сортировка</label>
      <div className="flex flex-wrap gap-2">
        {OPTIONS.map((o) => (
          <button
            key={o.value}
            onClick={() => onChange(o.value)}
            className={`px-3 py-1.5 text-xs font-medium rounded-md transition-colors ${
              value === o.value
                ? 'bg-brand-500 text-white shadow-lg shadow-brand-500/20'
                : 'bg-brand-800 text-gray-400 hover:text-white'
            }`}
          >
            {o.label}
          </button>
        ))}
      </div>
    </div>
  );
}
