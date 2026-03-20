import { useEffect } from 'react';
import { Outlet, useLocation } from 'react-router-dom';
import Navbar from './Navbar';
import ClipLangBootstrap from './ClipLangBootstrap';
import Footer from './Footer';
import FollowingSidebar, { SubscriptionsClipOverlayPortal } from './FollowingSidebar';
import useAuthStore from '../../store/useAuthStore';

export default function Layout() {
  const location = useLocation();
  const isClipPage = location.pathname.startsWith('/clip/') || location.pathname.startsWith('/feed');
  const isFeedPage = location.pathname.startsWith('/feed');
  const { token, user, fetchMe } = useAuthStore();

  useEffect(() => {
    if (token && !user) {
      fetchMe();
    }
  }, [token, user, fetchMe]);

  return (
    <div className="flex flex-col h-screen overflow-hidden bg-[#07090f]">
      <div className="pointer-events-none fixed inset-0 opacity-35 bg-[radial-gradient(circle_at_top_right,_rgba(145,70,255,0.22),transparent_45%),radial-gradient(circle_at_left_center,_rgba(39,180,255,0.14),transparent_38%)]" />
      <ClipLangBootstrap />
      <Navbar />
      <div className="flex-1 min-h-0 flex relative">
        <FollowingSidebar />
        <main
          className={`flex-1 relative flex flex-col min-h-0 ${isFeedPage ? 'overflow-hidden' : 'overflow-y-auto'}`}
          id="main-scroll"
        >
          <div className={isFeedPage ? 'flex-1 min-h-0 flex flex-col' : 'flex-1'}>
            <Outlet />
          </div>
          {!isFeedPage && <Footer />}
        </main>
        {isClipPage && <SubscriptionsClipOverlayPortal />}
      </div>
    </div>
  );
}
