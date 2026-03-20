import { Search, Play, LogOut, Flame, Home, Compass, UserRound, Wrench, Crown, Menu, Rows3 } from 'lucide-react';
import { useState } from 'react';
import { useNavigate, Link, useLocation } from 'react-router-dom';
import useAuthStore from '../../store/useAuthStore';
import TwitchLoginModal from '../auth/TwitchLoginModal';
import { shouldShowLegalConsentModal } from '../../constants/legalConsent';
import { startTwitchOAuthRedirect } from '../../utils/twitchOAuth';
import { useSubsSidebarStore } from '../../store/useSubsSidebarStore';
import HeaderLanguageMenu from './HeaderLanguageMenu';
import { useMediaMinXl } from '../../hooks/useMediaMinWidth';

export default function Navbar() {
  const [query, setQuery] = useState('');
  const navigate = useNavigate();
  const location = useLocation();
  const isClipPage = location.pathname.startsWith('/clip/');
  const isFeedPage = location.pathname.startsWith('/feed');
  const isClipLikePage = isClipPage || isFeedPage;
  const xl = useMediaMinXl();
  const toggleDockCollapsed = useSubsSidebarStore((s) => s.toggleDockCollapsed);
  const toggleClipOverlay = useSubsSidebarStore((s) => s.toggleClipOverlay);
  const dockCollapsed = useSubsSidebarStore((s) => s.dockCollapsed);
  const clipOverlayOpen = useSubsSidebarStore((s) => s.clipOverlayOpen);
  const [twitchModalOpen, setTwitchModalOpen] = useState(false);
  const { isAuth, user, logout } = useAuthStore();
  const staffAccess = user?.role === 1 || user?.role === 2;
  const isAdmin = user?.role === 2;

  const handleSearch = (e) => {
    e.preventDefault();
    if (!query.trim()) return;
    navigate(`/?q=${encodeURIComponent(query.trim())}`);
  };

  const openTwitchLogin = () => {
    if (shouldShowLegalConsentModal()) {
      setTwitchModalOpen(true);
      return;
    }
    void startTwitchOAuthRedirect().catch(() => setTwitchModalOpen(true));
  };

  /** Меню подписок / сворачивание гайда — нужно и гостям (иначе панель залипает в 72px без кнопки). */
  const showSubsMenu = isClipLikePage || xl;
  const handleSubsMenuClick = () => {
    if (isClipLikePage) toggleClipOverlay();
    else toggleDockCollapsed();
  };

  return (
    <nav className="h-16 border-b border-brand-700/60 bg-brand-900/85 backdrop-blur-xl flex items-center justify-between px-4 lg:px-6 z-20 shrink-0">
      <div className="flex items-center gap-3 min-w-0">
        {showSubsMenu && (
          <button
            type="button"
            onClick={handleSubsMenuClick}
            className="p-2 -ml-1 rounded-xl text-gray-400 hover:text-white hover:bg-brand-800/80 transition-colors shrink-0"
            aria-label={isClipLikePage ? 'Подписки Twitch' : 'Меню подписок'}
            aria-expanded={isClipLikePage ? clipOverlayOpen : !dockCollapsed}
            title={isClipLikePage ? 'Подписки' : 'Свернуть или развернуть меню подписок'}
          >
            <Menu className="w-6 h-6" strokeWidth={2.25} />
          </button>
        )}
        <Link to="/" className="flex items-center gap-2 group">
          <div className="w-8 h-8 bg-gradient-to-br from-brand-500 to-fuchsia-500 rounded-xl flex items-center justify-center text-white shadow-lg shadow-brand-500/30 group-hover:scale-110 transition-transform">
            <Play className="w-4 h-4 fill-current" />
          </div>
          <span className="text-lg font-black tracking-tight text-white hidden sm:block">
            Clip<span className="text-brand-500">Stream</span>
          </span>
        </Link>

        <div className="h-6 w-px bg-brand-700/70 mx-1 hidden lg:block" />

        <div className="hidden lg:flex items-center gap-1.5">
          <Link to="/" className="flex items-center gap-1.5 text-xs text-gray-300 hover:text-white transition-colors px-2.5 py-1.5 rounded-lg hover:bg-brand-800/70">
            <Home className="w-3.5 h-3.5" /> Главная
          </Link>
          <Link to="/categories" className="flex items-center gap-1.5 text-xs text-gray-300 hover:text-white transition-colors px-2.5 py-1.5 rounded-lg hover:bg-brand-800/70">
            <Compass className="w-3.5 h-3.5" /> Категории
          </Link>
          <Link to="/trends" className="flex items-center gap-1.5 text-xs text-gray-300 hover:text-white transition-colors px-2.5 py-1.5 rounded-lg hover:bg-brand-800/70">
            <Flame className="w-3.5 h-3.5 text-amber-400/90" /> Тренды
          </Link>
          <Link to="/feed" className="flex items-center gap-1.5 text-xs text-gray-300 hover:text-white transition-colors px-2.5 py-1.5 rounded-lg hover:bg-brand-800/70">
            <Rows3 className="w-3.5 h-3.5 text-brand-400" /> Лента
          </Link>
        </div>
      </div>

      <form onSubmit={handleSearch} className="flex-1 max-w-xl mx-4 hidden sm:block">
        <div className="relative group">
          <input
            type="text"
            value={query}
            onChange={(e) => setQuery(e.target.value)}
            placeholder="Поиск клипов..."
            className="w-full bg-brand-800/70 border border-brand-700 text-sm rounded-full py-2.5 px-4 pl-10 focus:outline-none focus:border-brand-500 focus:ring-1 focus:ring-brand-500 transition-all text-gray-200 placeholder-gray-500"
          />
          <Search className="absolute left-3.5 top-3 w-4 h-4 text-gray-500 group-focus-within:text-brand-500 transition-colors" />
        </div>
      </form>

      <div className="flex items-center gap-2">
        <HeaderLanguageMenu />
        {isAuth ? (
          <>
            {isAdmin && (
              <Link
                to="/admin"
                className="hidden sm:flex items-center gap-1.5 text-xs text-amber-300 hover:text-amber-200 px-2.5 py-1.5 rounded-lg hover:bg-amber-950/40 border border-amber-500/30"
                title="Панель администратора"
              >
                <Crown className="w-3.5 h-3.5" />
                <span className="hidden lg:inline font-medium">Админ</span>
              </Link>
            )}
            {staffAccess && (
              <Link
                to="/staff"
                className="hidden sm:flex items-center gap-1.5 text-xs text-amber-400/90 hover:text-amber-300 px-2.5 py-1.5 rounded-lg hover:bg-brand-800/70 border border-amber-500/20"
                title="Консоль персонала"
              >
                <Wrench className="w-3.5 h-3.5" />
                <span className="hidden lg:inline font-medium">Консоль</span>
              </Link>
            )}
            <Link
              to={user?.username ? `/u/${encodeURIComponent(user.username)}` : '/'}
              className="flex items-center gap-2 px-2 py-1.5 rounded-xl hover:bg-brand-800/80 transition-colors border border-transparent hover:border-brand-700/60"
            >
              <img
                src={user?.avatar_url || `https://api.dicebear.com/7.x/avataaars/svg?seed=${user?.username || 'U'}`}
                alt=""
                className="w-7 h-7 rounded-full border border-brand-500"
              />
              <span className="text-sm font-medium text-white hidden xl:block">{user?.username}</span>
              <UserRound className="w-3.5 h-3.5 text-brand-400 hidden xl:block" />
            </Link>
            <button
              onClick={logout}
              className="w-9 h-9 rounded-xl bg-brand-800/70 hover:bg-brand-700 flex items-center justify-center text-gray-400 hover:text-white transition-colors border border-brand-700/70"
              title="Выйти"
            >
              <LogOut className="w-3.5 h-3.5" />
            </button>
          </>
        ) : (
          <>
            <button
              type="button"
              onClick={openTwitchLogin}
              className="flex items-center gap-2 px-4 py-1.5 bg-[#9146FF] hover:bg-[#772CE8] text-white text-sm font-bold rounded-lg transition-colors"
            >
              <svg viewBox="0 0 24 24" className="w-4 h-4 fill-current">
                <path d="M11.64 5.93H13.07V10.21H11.64M15.57 5.93H17V10.21H15.57M7 2L3.43 5.57V18.43H7.71V22L11.29 18.43H14.14L20.57 12V2M19.14 11.29L16.29 14.14H13.43L10.93 16.64V14.14H7.71V3.43H19.14Z"/>
              </svg>
              Войти через Twitch
            </button>
            <TwitchLoginModal open={twitchModalOpen} onClose={() => setTwitchModalOpen(false)} />
          </>
        )}
      </div>
    </nav>
  );
}
