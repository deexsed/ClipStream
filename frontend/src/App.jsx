import { Routes, Route, Navigate } from 'react-router-dom';
import Layout from './components/layout/Layout';
import Home from './pages/Home';
import Profile from './pages/Profile';
import ClipPage from './pages/ClipPage';
import ClipFeedPage from './pages/ClipFeedPage';
import CategoriesPage from './pages/CategoriesPage';
import CategoryPage from './pages/CategoryPage';
import ChannelPage from './pages/ChannelPage';
import TrendsPage from './pages/TrendsPage';
import AuthCallback from './pages/AuthCallback';
import StaffConsole from './pages/StaffConsole';
import AdminDashboard from './pages/AdminDashboard';
import LegalPage from './pages/LegalPage';

export default function App() {
  return (
    <Routes>
      <Route element={<Layout />}>
        <Route path="/" element={<Home />} />
        <Route path="/legal" element={<LegalPage />} />
        <Route path="/legal/:slug" element={<LegalPage />} />
        <Route path="/u/:username" element={<Profile />} />
        <Route path="/clip/:id" element={<ClipPage />} />
        <Route path="/feed" element={<ClipFeedPage />} />
        <Route path="/categories" element={<CategoriesPage />} />
        <Route path="/trends" element={<TrendsPage />} />
        <Route path="/discover" element={<Navigate to="/trends" replace />} />
        <Route path="/category/:id" element={<CategoryPage />} />
        <Route path="/channel/:login" element={<ChannelPage />} />
        <Route path="/staff" element={<StaffConsole />} />
        <Route path="/admin" element={<AdminDashboard />} />
        <Route path="/auth/callback" element={<AuthCallback />} />
      </Route>
    </Routes>
  );
}
