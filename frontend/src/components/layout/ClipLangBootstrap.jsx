import { useEffect, useState } from 'react';
import useAuthStore from '../../store/useAuthStore';
import { useClipLangStore } from '../../store/useClipLangStore';
import { userSettings } from '../../api/client';

export default function ClipLangBootstrap() {
  const isAuth = useAuthStore((s) => s.isAuth);
  const codes = useClipLangStore((s) => s.codes);
  const [serverReady, setServerReady] = useState(false);

  useEffect(() => {
    if (!isAuth) {
      setServerReady(true);
      return;
    }
    setServerReady(false);
    let cancelled = false;
    userSettings
      .get()
      .then((s) => {
        if (cancelled) return;
        const langs = Array.isArray(s.clip_languages) ? s.clip_languages : [];
        useClipLangStore.getState().setCodes(langs);
      })
      .catch(() => {})
      .finally(() => {
        if (!cancelled) setServerReady(true);
      });
    return () => {
      cancelled = true;
    };
  }, [isAuth]);

  useEffect(() => {
    if (!isAuth || !serverReady) return;
    const t = setTimeout(() => {
      userSettings
        .get()
        .then((s) =>
          userSettings.save({
            hide_mature: s.hide_mature !== false,
            mute_keywords: Array.isArray(s.mute_keywords) ? s.mute_keywords : [],
            clip_languages: useClipLangStore.getState().codes,
          }),
        )
        .catch(() => {});
    }, 480);
    return () => clearTimeout(t);
  }, [isAuth, serverReady, codes]);

  return null;
}
