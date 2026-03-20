import { useClipLangStore } from '../store/useClipLangStore';

const BASE = '/api';

/** @param {string[]|undefined} overrideLangs undefined — коды из шапки; [] — все языки. */
function buildClipsFeedQuery(params, overrideLangs) {
  const sp =
    typeof params === 'string' || !params
      ? new URLSearchParams(params || '')
      : new URLSearchParams(params);
  const codes = overrideLangs === undefined ? useClipLangStore.getState().codes : overrideLangs;
  sp.set('clip_langs', Array.isArray(codes) ? codes.join(',') : '');
  return sp.toString();
}

/** Добавляет clip_langs ко всем запросам лент клипов (пустая строка = все языки). */
function withClipLangs(query) {
  return buildClipsFeedQuery(query, undefined);
}

async function request(path, options = {}) {
  const token = localStorage.getItem('token');
  const { cache, headers: optHeaders, ...fetchRest } = options;
  const headers = { 'Content-Type': 'application/json', ...optHeaders };
  if (token) headers['Authorization'] = `Bearer ${token}`;

  const res = await fetch(`${BASE}${path}`, {
    ...fetchRest,
    headers,
    cache: cache ?? 'default',
  });

  let data;
  const text = await res.text();
  try {
    data = text ? JSON.parse(text) : {};
  } catch {
    data = { raw: text };
  }

  if (!res.ok) {
    const message = (data && data.error) || `Request failed (${res.status})`;
    throw new Error(message);
  }
  return data;
}

export const api = {
  get:  (path) => request(path),
  post: (path, body) => request(path, { method: 'POST', body: JSON.stringify(body) }),
  put:  (path, body) => request(path, { method: 'PUT', body: JSON.stringify(body) }),
  del:  (path) => request(path, { method: 'DELETE' }),
};

export const clips = {
  feed: (params = '') => api.get(`/clips?${withClipLangs(params)}`),
  /** Явные языки для страницы «Тренды» и т.п. Пустой массив = без фильтра по языку. */
  feedWithLangs: (params = '', langCodes = []) => api.get(`/clips?${buildClipsFeedQuery(params, langCodes)}`),
  related: (id, params = '') =>
    request(`/clips/${id}/related?${withClipLangs(params || '')}`, { cache: 'no-store' }),
  home:     (limit = 12, sessionId = '') => {
    const q = new URLSearchParams({ limit: String(limit) });
    if (sessionId) q.set('session_id', sessionId);
    return api.get(`/clips/home?${withClipLangs(q)}`);
  },
  /** Смешанная лента главной (рекомендации + тренды + новинки). exclude — id через запятую (уже показанные). */
  homeFeed: (limit = 24, sessionId = '', exclude = '') => {
    const q = new URLSearchParams({ limit: String(limit) });
    if (sessionId) q.set('session_id', sessionId);
    if (exclude) q.set('exclude', exclude);
    return api.get(`/clips/home-feed?${withClipLangs(q)}`);
  },
  get:      (id) => api.get(`/clips/${id}`),
  search:   (q, limit = 20) => {
    const sp = new URLSearchParams({ q, limit: String(limit) });
    return api.get(`/clips/search?${withClipLangs(sp)}`);
  },
  view:     (id) => api.post(`/clips/${id}/view`),
  engagement: (id, body) => api.post(`/clips/${id}/engagement`, body),
  feedback: (id, action) => api.post(`/clips/${id}/feedback`, { action }),
  like:     (id, isLike) => api.post(`/clips/${id}/like`, { is_like: isLike }),
  report:   (id, reason) => api.post(`/clips/${id}/report`, { reason }),
};

export const comments = {
  list:   (clipId, limit = 20, offset = 0) => api.get(`/clips/${clipId}/comments?limit=${limit}&offset=${offset}`),
  create: (clipId, body, parentId) => api.post(`/clips/${clipId}/comments`, { body, parent_id: parentId }),
  update: (id, body) => api.put(`/comments/${id}`, { body }),
  remove: (id) => api.del(`/comments/${id}`),
};

export const games = {
  list: () => api.get('/games'),
};

export const streams = {
  live: (params = '') => api.get(`/streams/live${params ? `?${params}` : ''}`),
  isLive: (broadcasterIds) => api.get(`/streams/check?ids=${broadcasterIds.join(',')}`),
};

export const channels = {
  search: (q, limit = 20) => api.get(`/channels/search?q=${encodeURIComponent(q)}&limit=${limit}`),
  followed: () => api.get('/channels/followed'),
  metadata: (ids = []) => api.get(`/channels/metadata?ids=${ids.join(',')}`),
  similar: (limit = 8) => api.get(`/channels/similar?limit=${limit}`),
  profile: (login) => api.get(`/channels/profile?login=${encodeURIComponent(login)}`),
};

export const admin = {
  analyticsSummary: () => api.get('/admin/analytics/summary'),
  dashboard: () => api.get('/admin/dashboard'),
  users: (params = '') => api.get(`/admin/users?${params}`),
  setUserRole: (userId, role) => api.post(`/admin/users/${userId}/role`, { role }),
  promotions: (params = '') => api.get(`/admin/promotions?${params}`),
  createPromotion: (body) => api.post('/admin/promotions', body),
  deletePromotion: (id) => api.del(`/admin/promotions/${id}`),
};

export const staff = {
  bootstrap: () => api.get('/staff/bootstrap'),
  diagnostics: () => api.get('/staff/diagnostics'),
  contentReports: (params = '') => api.get(`/staff/content-reports?${params}`),
};

export const social = {
  publicProfile: (username) => api.get(`/users/public/${encodeURIComponent(username)}`),
  follow: (userId) => api.post(`/social/follow/${userId}`),
  unfollow: (userId) => api.del(`/social/follow/${userId}`),
  friendsFeed: (params = '') => api.get(`/social/friends-feed?${withClipLangs(params)}`),
  followers: (userId, params = '') => api.get(`/social/users/${userId}/followers?${params}`),
  following: (userId, params = '') => api.get(`/social/users/${userId}/following?${params}`),
  userClips: (userId, params = '') => api.get(`/social/users/${userId}/clips?${withClipLangs(params)}`),
  watchActivity: (userId, params = '') => api.get(`/social/users/${userId}/watch-activity?${params}`),
  searchUsers: (q, limit = 15) => api.get(`/users/search?q=${encodeURIComponent(q)}&limit=${limit}`),
};

export const userSettings = {
  get: () => api.get('/user/settings'),
  save: (body) => api.put('/user/settings', body),
};

export const moderation = {
  restriction: (userId, body) => api.post(`/moderation/users/${userId}/restriction`, body),
};

export const auth = {
  register: (username, email, password) => api.post('/auth/register', { username, email, password }),
  login:    (email, password) => api.post('/auth/login', { email, password }),
  me:       () => api.get('/auth/me'),
  twitchUrl: () => api.get('/auth/twitch/url'),
  twitchLogin: (code, state, consent) => {
    const body = { code, state };
    if (consent && typeof consent === 'object') body.consent = consent;
    return api.post('/auth/twitch', body);
  },
};
