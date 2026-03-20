import { create } from 'zustand';
import { auth as authApi } from '../api/client';
import {
  LEGAL_CONSENT_VERSION,
  OAUTH_LEGAL_STORAGE_KEY,
  rememberLegalConsentAck,
  forgetLegalConsentAck,
} from '../constants/legalConsent';

const useAuthStore = create((set) => ({
  user: null,
  profile: null,
  token: localStorage.getItem('token'),
  isAuth: !!localStorage.getItem('token'),
  isLoading: false,

  handleTwitchCallback: async (code, state) => {
    const raw = sessionStorage.getItem(OAUTH_LEGAL_STORAGE_KEY);
    let consent;
    if (raw) {
      let parsed;
      try {
        parsed = JSON.parse(raw);
      } catch {
        throw new Error('Некорректные данные согласия. Повторите вход через Twitch.');
      }
      if (parsed.version !== LEGAL_CONSENT_VERSION) {
        throw new Error('Обновились документы. Повторите вход и согласие с новой версией.');
      }
      consent = { version: parsed.version, accepted_at: parsed.accepted_at };
    }

    try {
      const { token, user } = await authApi.twitchLogin(code, state, consent);
      sessionStorage.removeItem(OAUTH_LEGAL_STORAGE_KEY);
      rememberLegalConsentAck();
      localStorage.setItem('token', token);
      set({ user, token, isAuth: true, profile: null });
    } catch (e) {
      const msg = e?.message || '';
      if (/согласие|consent/i.test(msg)) {
        forgetLegalConsentAck();
      }
      throw e;
    }
  },

  login: async (email, password) => {
    const { token, user } = await authApi.login(email, password);
    localStorage.setItem('token', token);
    set({ user, token, isAuth: true, profile: null });
  },

  register: async (username, email, password) => {
    const { token, user } = await authApi.register(username, email, password);
    localStorage.setItem('token', token);
    set({ user, token, isAuth: true, profile: null });
  },

  logout: () => {
    localStorage.removeItem('token');
    set({ user: null, profile: null, token: null, isAuth: false });
  },

  fetchMe: async () => {
    try {
      const data = await authApi.me();
      if (data.user?.legal_consent_version === LEGAL_CONSENT_VERSION) {
        rememberLegalConsentAck();
      }
      set({ user: data.user, profile: data });
    } catch {
      localStorage.removeItem('token');
      set({ user: null, profile: null, token: null, isAuth: false });
    }
  },
}));

export default useAuthStore;
