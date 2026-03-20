import { auth as authApi } from '../api/client';

/** Редирект на Twitch без модалки согласия (уже принято на этом устройстве и актуальная версия). */
export async function startTwitchOAuthRedirect() {
  const { url } = await authApi.twitchUrl();
  window.location.href = url;
}
