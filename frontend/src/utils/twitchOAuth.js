import { auth as authApi } from '../api/client';

export async function startTwitchOAuthRedirect() {
  const { url } = await authApi.twitchUrl();
  window.location.href = url;
}
