import { useState, useEffect } from 'react';
import { useParams, useNavigate, Link } from 'react-router-dom';
import { ChevronRight, ArrowLeft, Flame, Clock, Timer } from 'lucide-react';
import { clips as clipsApi, games as gamesApi } from '../api/client';
import ClipGrid from '../components/clips/ClipGrid';
import Loader from '../components/common/Loader';
import useLiveStatus from '../hooks/useLiveStatus';
import { useClipLangStore } from '../store/useClipLangStore';

const SORTS = [
  { key: 'popular', label: 'Популярные', icon: Flame },
  { key: 'recent',  label: 'Новые',      icon: Clock },
  { key: 'duration', label: 'Длинные',   icon: Timer },
];

export default function CategoryPage() {
  const { id } = useParams();
  const navigate = useNavigate();
  const [game, setGame] = useState(null);
  const [clipsList, setClipsList] = useState([]);
  const [loading, setLoading] = useState(true);
  const [sort, setSort] = useState('popular');
  const [offset, setOffset] = useState(0);
  const [hasMore, setHasMore] = useState(true);
  const liveMap = useLiveStatus(clipsList);
  const clipLangKey = useClipLangStore((s) => s.codes.join(','));

  useEffect(() => {
    gamesApi.list().then((data) => {
      const g = (data.games || []).find((g) => String(g.id) === id);
      setGame(g || null);
    });
  }, [id]);

  useEffect(() => {
    setLoading(true);
    setClipsList([]);
    setOffset(0);
    setHasMore(true);
    fetchClips(0, sort);
  }, [id, sort, clipLangKey]);

  const fetchClips = (off, s) => {
    const params = new URLSearchParams({
      game_id: id,
      sort: s,
      period: 'all',
      limit: '20',
      offset: String(off),
    });
    clipsApi.feed(params.toString())
      .then((data) => {
        const newClips = data.clips || [];
        setClipsList((prev) => off === 0 ? newClips : [...prev, ...newClips]);
        setOffset(off + newClips.length);
        setHasMore(newClips.length >= 20);
      })
      .catch(() => {})
      .finally(() => setLoading(false));
  };

  const loadMore = () => {
    if (!hasMore || loading) return;
    setLoading(true);
    fetchClips(offset, sort);
  };

  return (
    <div className="p-6 lg:p-8 max-w-[1700px] mx-auto">
      <div className="flex items-center gap-2 text-sm text-gray-400 mb-6">
        <Link to="/" className="hover:text-white transition-colors">Главная</Link>
        <ChevronRight className="w-3 h-3" />
        <Link to="/categories" className="hover:text-white transition-colors">Категории</Link>
        <ChevronRight className="w-3 h-3" />
        <span className="text-white font-medium">{game?.name || 'Загрузка...'}</span>
      </div>

      <div className="flex flex-col sm:flex-row sm:items-end justify-between gap-4 mb-6 rounded-2xl border border-brand-700/50 bg-brand-900/60 p-4">
        <div className="flex items-center gap-4">
          {game?.box_art && (
            <img src={game.box_art} className="w-16 h-20 rounded-lg object-cover border border-brand-700/50" alt="" />
          )}
          <div>
            <h1 className="text-2xl font-bold text-white">{game?.name}</h1>
            <p className="text-gray-400 text-sm mt-1">{game?.clip_count || 0} клипов</p>
          </div>
        </div>

        <div className="flex gap-2">
          {SORTS.map(({ key, label, icon: Icon }) => (
            <button
              key={key}
              onClick={() => setSort(key)}
              className={`flex items-center gap-1.5 px-3 py-1.5 rounded-lg text-sm font-medium transition-colors ${
                sort === key
                  ? 'bg-brand-500 text-white shadow-lg shadow-brand-500/20'
                  : 'bg-brand-800 text-gray-400 hover:text-white hover:bg-brand-700 border border-brand-700'
              }`}
            >
              <Icon className="w-3.5 h-3.5" /> {label}
            </button>
          ))}
        </div>
      </div>

      {clipsList.length === 0 && !loading ? (
        <div className="text-center py-20">
          <p className="text-gray-500 text-lg">Клипов пока нет для этой категории</p>
          <button onClick={() => navigate('/categories')} className="mt-4 px-4 py-2 bg-brand-500 text-white rounded-lg text-sm">
            <ArrowLeft className="w-4 h-4 inline mr-1" /> К категориям
          </button>
        </div>
      ) : (
        <>
          <ClipGrid clips={clipsList} onClipClick={(clip) => navigate(`/clip/${clip.id}`)} liveMap={liveMap} />
          {hasMore && (
            <div className="flex justify-center mt-8">
              <button
                onClick={loadMore}
                disabled={loading}
                className="px-6 py-2.5 bg-brand-800 hover:bg-brand-700 text-white rounded-lg border border-brand-700 transition-colors disabled:opacity-50"
              >
                {loading ? 'Загрузка...' : 'Показать ещё'}
              </button>
            </div>
          )}
          {loading && <Loader />}
        </>
      )}
    </div>
  );
}
