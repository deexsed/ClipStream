# ClipStream

Агрегатор клипов Twitch: лента, тренды, категории, профили, комментарии, друзья.

## Стек

React 18, Vite, Tailwind, Zustand | C++20, Boost.Beast | MySQL 8, Redis 7 | Docker, Nginx

## Запуск

```bash
cp .env.example .env
docker compose up --build -d
```

В `.env` задать `TWITCH_CLIENT_ID`, `TWITCH_CLIENT_SECRET`, `JWT_SECRET`, пароли MySQL.  
`TWITCH_REDIRECT_URI=http://localhost/auth/callback` - тот же URL в [консоли Twitch](https://dev.twitch.tv/console/apps).

Открыть http://localhost .

Порты на хосте: 80 (сайт), 8080 (backend), 3000 (frontend), 3307 (MySQL), 6379 (Redis).

Проверка:

```bash
curl http://localhost/api/health
```

Если после перезапуска backend ответы `/api/*` дают 502:

```bash
docker compose up -d --force-recreate --no-deps nginx
```

## Twitch OAuth

1. Redirect URL в Twitch: `http://localhost/auth/callback`
2. Вход на сайте через Twitch, callback на `/auth/callback`
3. `LEGAL_CONSENT_VERSION` в `frontend/src/constants/legalConsent.js` и `kLegalConsentVersion` в `backend/src/controllers/AuthController.cpp` должны совпадать