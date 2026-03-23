import { Link } from 'react-router-dom';
import { Home } from 'lucide-react';

/**
 * Оболочка профиля: обложка на всю ширину main, контент — на всю рабочую область (до ~1920px),
 * сетка «узкая навигация + растущая колонка контента».
 *
 * @param {object} props
 * @param {string} [props.coverUrl] — фон обложки (картинка)
 * @param {'user'|'twitch'} [props.flavor]
 * @param {string} props.avatarUrl
 * @param {import('react').ReactNode} [props.avatarBadge]
 * @param {string} props.title
 * @param {import('react').ReactNode} [props.titleBadges]
 * @param {import('react').ReactNode} [props.subtitle]
 * @param {import('react').ReactNode} [props.tagRow]
 * @param {import('react').ReactNode} [props.actionRow]
 * @param {{ id: string, label: string, icon?: import('lucide-react').LucideIcon, active: boolean, onClick: () => void }[]} props.sidebarNav
 * @param {import('react').ReactNode} [props.sidebarFooter]
 * @param {import('react').ReactNode} props.children — основная колонка (лента / разделы)
 */
export default function SocialProfileLayout({
  coverUrl,
  flavor = 'user',
  avatarUrl,
  avatarBadge,
  title,
  titleBadges,
  subtitle,
  tagRow,
  actionRow,
  sidebarNav,
  sidebarFooter,
  children,
}) {
  const coverStyle = coverUrl
    ? {
        backgroundImage: `linear-gradient(to bottom, rgba(10,6,18,0.2) 0%, rgba(10,6,18,0.88) 100%), url(${coverUrl})`,
        backgroundSize: 'cover',
        backgroundPosition: 'center top',
      }
    : undefined;

  /** Одна колонка с отступами на всю рабочую область основного блока (без узкого max-w). */
  const shell =
    'w-full max-w-[min(100%,1920px)] mx-auto px-3 sm:px-5 md:px-6 lg:px-8 xl:px-10';

  return (
    <div className="pb-20 w-full min-w-0">
      <div className={shell}>
        <Link
          to="/"
          className="inline-flex items-center gap-1.5 text-xs text-gray-500 hover:text-brand-300 py-2.5 transition-colors"
        >
          <Home className="w-3.5 h-3.5 shrink-0" />
          На главную
        </Link>
      </div>

      <div
        className={`relative h-[12rem] sm:h-[14rem] md:h-[16rem] lg:h-[17.5rem] w-full ${
          flavor === 'twitch'
            ? 'bg-gradient-to-br from-[#3d2f5c] via-[#231a35] to-[#0c0712]'
            : 'bg-gradient-to-br from-violet-900/50 via-brand-950 to-[#08050e]'
        }`}
        style={coverStyle}
      >
        <div className="absolute inset-0 bg-gradient-to-t from-[#0c0712] from-[35%] via-[#0c0712]/65 to-transparent pointer-events-none" />
      </div>

      <div
        className={`${shell} relative z-[1] -mt-[4.25rem] sm:-mt-[5rem] md:-mt-[5.5rem] lg:-mt-28`}
      >
        <div className="flex flex-col sm:flex-row sm:items-end gap-5 sm:gap-7 lg:gap-10">
          <div className="relative shrink-0 w-[7rem] h-[7rem] sm:w-[8rem] sm:h-[8rem] md:w-[8.75rem] md:h-[8.75rem] lg:w-[9.5rem] lg:h-[9.5rem] rounded-2xl overflow-hidden ring-4 ring-[#0c0712] shadow-2xl shadow-black/60 bg-brand-900">
            <img src={avatarUrl} alt="" className="w-full h-full object-cover" />
            {avatarBadge}
          </div>
          <div className="flex-1 min-w-0 pb-0.5 sm:pb-1">
            <div className="flex flex-wrap items-center gap-2 gap-y-1">
              <h1 className="text-2xl sm:text-3xl lg:text-4xl font-bold text-white tracking-tight drop-shadow-sm">
                {title}
              </h1>
              {titleBadges}
            </div>
            {subtitle ? <div className="text-sm sm:text-base text-gray-400 mt-1.5">{subtitle}</div> : null}
            {tagRow ? <div className="flex flex-wrap gap-2 mt-3">{tagRow}</div> : null}
            {actionRow ? <div className="flex flex-wrap gap-2 mt-3.5">{actionRow}</div> : null}
          </div>
        </div>

        <div className="mt-8 lg:mt-11 xl:mt-12 grid grid-cols-1 lg:grid-cols-[minmax(17rem,20rem)_minmax(0,1fr)] xl:grid-cols-[minmax(18rem,21rem)_minmax(0,1fr)] gap-6 lg:gap-8 xl:gap-10 items-start">
          <aside className="order-2 lg:order-1 lg:sticky lg:top-4 space-y-4">
            <nav
              className="rounded-xl border border-brand-700/50 bg-[#12101a]/95 backdrop-blur-md overflow-hidden shadow-xl shadow-black/30"
              aria-label="Меню профиля"
            >
              <div className="px-3.5 py-2.5 border-b border-brand-800/90 text-[10px] font-bold uppercase tracking-[0.12em] text-gray-500">
                {flavor === 'twitch' ? 'Меню канала' : 'Навигация'}
              </div>
              <ul className="py-2">
                {sidebarNav.map((item) => {
                  const Icon = item.icon;
                  return (
                    <li key={item.id}>
                      <button
                        type="button"
                        onClick={item.onClick}
                        className={`w-full flex items-center gap-3 px-3.5 py-2.5 text-left text-[13px] lg:text-[13.5px] transition-colors rounded-lg mx-1.5 my-0.5 ${
                          item.active
                            ? flavor === 'twitch'
                              ? 'bg-[#6441a4]/35 text-white font-semibold border border-[#7d5bbe]/30'
                              : 'bg-brand-600/30 text-white font-semibold border border-brand-500/25'
                            : 'text-gray-400 hover:bg-brand-800/60 hover:text-gray-100 border border-transparent'
                        }`}
                      >
                        {Icon ? <Icon className="w-[18px] h-[18px] shrink-0 opacity-90" /> : null}
                        <span className="truncate">{item.label}</span>
                      </button>
                    </li>
                  );
                })}
              </ul>
            </nav>
            {sidebarFooter ? (
              <div className="rounded-xl border border-brand-700/45 bg-[#12101a]/85 backdrop-blur-sm p-3.5 text-xs shadow-lg shadow-black/20">
                {sidebarFooter}
              </div>
            ) : null}
          </aside>

          <main className="min-w-0 order-1 lg:order-2 space-y-5 lg:space-y-6">{children}</main>
        </div>
      </div>
    </div>
  );
}
