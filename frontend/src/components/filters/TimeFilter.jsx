const OPTIONS = [
  { value: 'today', label: 'Сегодня' },
  { value: 'week',  label: 'Эта неделя' },
  { value: 'month', label: 'Этот месяц' },
  { value: 'all',   label: 'Все время' },
];

export default function TimeFilter({ value, onChange }) {
  return (
    <div className="mb-6">
      <label className="block text-sm font-medium text-gray-300 mb-2">За период</label>
      <select
        value={value}
        onChange={(e) => onChange(e.target.value)}
        className="w-full bg-brand-800 border border-brand-700 text-sm rounded-lg p-2.5 text-gray-300 focus:border-brand-500 focus:outline-none"
      >
        {OPTIONS.map((o) => (
          <option key={o.value} value={o.value}>{o.label}</option>
        ))}
      </select>
    </div>
  );
}
