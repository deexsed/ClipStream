import { useState, useEffect } from 'react';
import { games as gamesApi } from '../../api/client';

export default function GameFilter({ value, onChange }) {
  const [gamesList, setGamesList] = useState([]);

  useEffect(() => {
    gamesApi.list()
      .then((data) => setGamesList(data.games || []))
      .catch(() => {});
  }, []);

  return (
    <div className="mb-6">
      <label className="block text-sm font-medium text-gray-300 mb-2">Категория</label>
      <div className="space-y-2">
        <label className="flex items-center gap-3 p-2 rounded-lg hover:bg-brand-800 cursor-pointer transition-colors group">
          <input
            type="radio"
            name="game"
            checked={!value}
            onChange={() => onChange('')}
            className="accent-brand-500 w-4 h-4"
          />
          <span className="text-sm text-gray-300 group-hover:text-white">Все категории</span>
        </label>
        {gamesList.map((g) => (
          <label
            key={g.id}
            className="flex items-center gap-3 p-2 rounded-lg hover:bg-brand-800 cursor-pointer transition-colors group"
          >
            <input
              type="radio"
              name="game"
              checked={value === String(g.id)}
              onChange={() => onChange(String(g.id))}
              className="accent-brand-500 w-4 h-4"
            />
            <span className="text-sm text-gray-300 group-hover:text-white">{g.name}</span>
            <span className="ml-auto text-xs text-gray-600 bg-brand-800 px-2 py-0.5 rounded">
              {g.clip_count}
            </span>
          </label>
        ))}
      </div>
    </div>
  );
}
