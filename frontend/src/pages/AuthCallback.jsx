import { useEffect, useState } from 'react';
import { useNavigate, useSearchParams } from 'react-router-dom';
import useAuthStore from '../store/useAuthStore';

/** Переживают remount (React Strict Mode), чтобы не дергать Twitch дважды и не терять code после replace. */
let oauthCodeCaptured = null;
let oauthStateCaptured = '';
let oauthCallbackStarted = false;

function resetOauthFlow() {
  oauthCallbackStarted = false;
  oauthCodeCaptured = null;
  oauthStateCaptured = '';
}

export default function AuthCallback() {
  const [searchParams] = useSearchParams();
  const navigate = useNavigate();
  const { handleTwitchCallback } = useAuthStore();
  const [error, setError] = useState('');

  useEffect(() => {
    const codeFromUrl = searchParams.get('code');
    if (codeFromUrl) {
      if (oauthCodeCaptured && oauthCodeCaptured !== codeFromUrl) resetOauthFlow();
      oauthCodeCaptured = codeFromUrl;
      oauthStateCaptured = searchParams.get('state') || '';
    }
    const code = oauthCodeCaptured;
    if (!code) {
      setError('Код авторизации не найден');
      return;
    }
    if (oauthCallbackStarted) return;
    oauthCallbackStarted = true;
    navigate('/auth/callback', { replace: true });
    handleTwitchCallback(code, oauthStateCaptured)
      .then(() => {
        resetOauthFlow();
        navigate('/');
      })
      .catch((err) => {
        resetOauthFlow();
        setError(err.message || 'Ошибка авторизации');
      });
  }, [searchParams, navigate, handleTwitchCallback]);

  if (error) {
    return (
      <div className="flex items-center justify-center h-full">
        <div className="text-center">
          <div className="text-red-400 text-xl font-bold mb-2">Ошибка</div>
          <p className="text-gray-400">{error}</p>
          <button onClick={() => navigate('/')} className="mt-4 px-4 py-2 bg-brand-500 text-white rounded-lg">
            На главную
          </button>
        </div>
      </div>
    );
  }

  return (
    <div className="flex items-center justify-center h-full">
      <div className="text-center">
        <div className="flex gap-2 justify-center mb-4">
          <div className="w-3 h-3 bg-brand-500 rounded-full animate-bounce" />
          <div className="w-3 h-3 bg-brand-500 rounded-full animate-bounce" style={{ animationDelay: '0.1s' }} />
          <div className="w-3 h-3 bg-brand-500 rounded-full animate-bounce" style={{ animationDelay: '0.2s' }} />
        </div>
        <p className="text-gray-400">Авторизация через Twitch...</p>
      </div>
    </div>
  );
}
