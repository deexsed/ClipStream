const TAGS = ['Roshan', 'Pudge', 'Funny', 'Pro', 'Clutch', 'Rampage'];

export default function TagFilter({ value, onChange }) {
  return (
    <div>
      <label className="block text-sm font-medium text-gray-300 mb-2">Теги</label>
      <div className="flex flex-wrap gap-2">
        {TAGS.map((tag) => (
          <span
            key={tag}
            onClick={() => onChange(value === tag ? '' : tag)}
            className={`px-2.5 py-1 text-xs rounded-full cursor-pointer transition-colors border ${
              value === tag
                ? 'bg-brand-500/20 border-brand-500 text-brand-500'
                : 'bg-brand-800/50 border-brand-700/50 text-gray-400 hover:border-brand-500 hover:text-brand-500'
            }`}
          >
            #{tag}
          </span>
        ))}
      </div>
    </div>
  );
}
