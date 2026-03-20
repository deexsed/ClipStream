import { useState, useEffect } from 'react';
import { useNavigate } from 'react-router-dom';
import { Gamepad2, TrendingUp } from 'lucide-react';
import { games as gamesApi } from '../api/client';

export default function CategoriesPage() {
  const [gamesList, setGamesList] = useState([]);
  const [loading, setLoading] = useState(true);
  const navigate = useNavigate();

  useEffect(() => {
    gamesApi.list()
      .then((data) => setGamesList(data.games || []))
      .catch(() => {})
      .finally(() => setLoading(false));
  }, []);

  return (
    <div className="p-6 lg:p-8 max-w-[1700px] mx-auto">
      <div className="mb-6 animate-fade-in rounded-2xl border border-brand-700/50 bg-gradient-to-br from-brand-900 to-brand-800 p-5">
        <h1 className="text-3xl font-black text-white flex items-center gap-3">
          <Gamepad2 className="w-8 h-8 text-brand-500" />
          Исследуй категории
        </h1>
        <p className="text-gray-400 mt-2">Подборка игр с наибольшим количеством клипов и трендов</p>
      </div>

      {loading ? (
        <div className="flex justify-center py-20">
          <div className="flex gap-2">
            <div className="w-3 h-3 bg-brand-500 rounded-full animate-bounce" />
            <div className="w-3 h-3 bg-brand-500 rounded-full animate-bounce" style={{ animationDelay: '0.1s' }} />
            <div className="w-3 h-3 bg-brand-500 rounded-full animate-bounce" style={{ animationDelay: '0.2s' }} />
          </div>
        </div>
      ) : (
        <div className="grid grid-cols-2 sm:grid-cols-3 md:grid-cols-4 lg:grid-cols-5 xl:grid-cols-6 2xl:grid-cols-7 gap-4">
          {gamesList.map((game, i) => (
            <div
              key={game.id}
              onClick={() => navigate(`/category/${game.id}`)}
              className="group cursor-pointer animate-fade-in"
              style={{ animationDelay: `${i * 0.03}s` }}
            >
              <div className="relative aspect-[3/4] rounded-xl overflow-hidden bg-brand-800 border border-brand-700/50 group-hover:border-brand-500/50 transition-all duration-300 group-hover:shadow-xl group-hover:shadow-brand-500/10 group-hover:-translate-y-1">
                {game.box_art && game.box_art !== null ? (
                  <img
                    src={game.box_art}
                    alt={game.name}
                    className="w-full h-full object-cover transition-transform duration-500 group-hover:scale-105"
                    loading="lazy"
                    onError={(e) => { e.target.style.display = 'none'; }}
                  />
                ) : (
                  <div className="w-full h-full flex items-center justify-center bg-gradient-to-br from-brand-800 to-brand-900">
                    <Gamepad2 className="w-12 h-12 text-brand-700" />
                  </div>
                )}
                <div className="absolute inset-0 bg-gradient-to-t from-black/80 via-transparent to-transparent opacity-0 group-hover:opacity-100 transition-opacity" />
              </div>
              <div className="mt-2 px-1">
                <h3 className="text-sm font-bold text-white truncate group-hover:text-brand-400 transition-colors">
                  {game.name}
                </h3>
                <div className="flex items-center gap-1 text-xs text-gray-500 mt-0.5">
                  <TrendingUp className="w-3 h-3" />
                  {game.clip_count} клипов
                </div>
              </div>
            </div>
          ))}
        </div>
      )}
    </div>
  );
}
