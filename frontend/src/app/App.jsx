import { lazy, Suspense } from "react";
import { Navigate, Route, Routes } from "react-router-dom";
import Shell from "./Shell";
import { Status } from "../shared/ui/Primitives";

const Discover = lazy(() => import("../features/discover/DiscoverPage"));
const Clip = lazy(() => import("../features/clips/ClipPage"));
const Profile = lazy(() => import("../features/social/ProfilePage"));
const Following = lazy(() => import("../features/social/FollowingPage"));
const Console = lazy(() => import("../features/admin/ConsolePage"));
const Legal = lazy(() => import("../features/legal/LegalPage"));
const Callback = lazy(() => import("../features/auth/CallbackPage"));

export default function App() {
  return (
    <Suspense fallback={<Status loading />}>
      <Routes>
        <Route element={<Shell />}>
          <Route index element={<Discover />} />
          <Route path="categories" element={<Discover mode="categories" />} />
          <Route path="category/:id" element={<Discover mode="category" />} />
          <Route path="trends" element={<Discover mode="trends" />} />
          <Route path="discover" element={<Navigate to="/trends" replace />} />
          <Route path="feed" element={<Clip feed />} />
          <Route path="following" element={<Following />} />
          <Route path="clip/:id" element={<Clip />} />
          <Route path="u/:username" element={<Profile />} />
          <Route path="channel/:login" element={<Profile source="login" />} />
          <Route path="p/twitch/:id" element={<Profile source="twitch" />} />
          <Route path="p/user/:username" element={<Profile />} />
          <Route path="staff" element={<Console />} />
          <Route path="admin" element={<Console admin />} />
          <Route path="legal/:slug?" element={<Legal />} />
          <Route path="auth/callback" element={<Callback />} />
          <Route
            path="*"
            element={
              <div className="page">
                <Status empty="Такой страницы нет" />
                <a className="button button-primary" href="/">
                  На главную
                </a>
              </div>
            }
          />
        </Route>
      </Routes>
    </Suspense>
  );
}
