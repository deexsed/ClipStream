# Настройка Twitch OAuth

Вход в ClipStream выполняется только через Twitch Authorization Code Flow. Client Secret используется сервером, а не frontend. App access token сам по себе не авторизует пользователя.

## Локальный запуск

1. В [Twitch Developer Console](https://dev.twitch.tv/console/apps) создайте или откройте своё приложение. Для регистрации приложения Twitch требует включённую двухфакторную аутентификацию. Приложение использует серверный Client Secret.
2. Добавьте OAuth Redirect URL **`http://localhost/auth/callback`**. Он должен совпадать с `TWITCH_REDIRECT_URI` и адресом, с которого начинается вход. Не смешивайте `localhost` и `127.0.0.1`.
3. Скопируйте `.env.example` в `.env`, выполните `pwsh deploy/init-local.ps1`. Укажите `TWITCH_CLIENT_ID` в `.env`. Сохраните Client Secret одной строкой в `temp/secrets/twitch_client_secret` через редактор. Не помещайте секрет в Git, командную строку, URL или frontend. Генератор сохраняет уже существующие секреты.
4. Запустите `docker compose --env-file .env up --build -d`. После изменения файла секрета повторно запустите одноразовый `secrets-init` и пересоздайте API/worker, чтобы процессы прочитали новое значение:

   ```powershell
   docker compose --env-file .env run --rm secrets-init
   docker compose --env-file .env up -d --force-recreate api worker
   ```

5. Откройте **http://localhost**, нажмите «Войти», прочитайте документы, подтвердите согласие и продолжите на Twitch. Пароль вводится на стороне Twitch. После возврата профиль должен открываться без `code` в адресной строке.
6. Проверьте `http://localhost/api/config`: `auth.twitch_enabled` должен быть `true`, `local_enabled` — `false`. Ответ не содержит секретов. `/api/health/ready` проверяет готовность зависимостей, но не подтверждает успешный OAuth.

Запросы разрешений: `user:read:email`, `user:read:follows`. Согласие с документами ClipStream и выдача разрешений Twitch — разные действия. Можно отказаться от входа и пользоваться открытыми разделами.

## Администратор deexsd

При самой первой миграции **пустой** базы сохраняется право bootstrap. Подтверждённый через Twitch вход `deexsd` получает admin по неизменяемому Twitch ID, разрешённому сервером через Helix. Другой пользователь может войти раньше и не перехватит admin. Имя локального профиля и email не участвуют в этой проверке. Для ранее непустой базы bootstrap отключён: её нельзя очищать ради назначения роли. Существующий admin назначает роли через `/admin`.

## Публичный домен и несколько серверов

Используйте точный `https://<ваш-домен>/auth/callback` и добавьте его в Developer Console. Настройте доверенный TLS edge, одинаковые JWT/gateway secrets, общие MySQL и Redis для всех реплик. Браузеру необходим HTTPS secure context для Web Crypto; исключение — loopback localhost при разработке. Callback должен обслуживаться SPA, `/api` — API. Не кэшируйте ответы авторизации и не записывайте query string callback в access log.

OAuth state привязан к случайному verifier в текущей вкладке, проверяется сервером через shared Redis и погашается однократно. Это внутренняя привязка браузера, а не заявление о поддержке Twitch PKCE. Не копируйте callback в другую вкладку; после ошибки начните вход заново.

## Обслуживание и диагностика

| Симптом | Проверка |
| --- | --- |
| Кнопка недоступна / 503 | Client ID, непустой файл секрета, запуск secrets-init, пересоздание API |
| Redirect mismatch | Полное совпадение scheme, host, port, path в Twitch Console и конфигурации |
| State expired / invalid | Начать вход заново в той же вкладке; проверить доступность shared Redis |
| 429 | Не повторять запросы циклом; учитывать retry_after, проверить общий лимитер Redis |
| 401 после отзыва доступа | Повторно войти через Twitch; не восстанавливать старый JWT вручную |
| 503 при временном сбое | Повторить позже: клиент сохраняет сеанс при сетевых ошибках |
| Нет admin в старой базе | Проверить bootstrap eligibility и существующего администратора, не удалять данные |

Выход отзывает сеансы ClipStream; отключение приложения выполняется в настройках подключений Twitch. Refresh и проверка токенов выполняются сервером с общей блокировкой между репликами. EventSub отдельно требует реального вошедшего пользователя и `TWITCH_EVENTSUB_USER_ID`; отсутствие EventSub не должно мешать обычному входу.

Ранее переданный в чат Client Secret следует заменить в Twitch Console перед публичным развёртыванием. Старый секрет в исходники не записан. Ротация нового секрета описана выше; Swarm rotation — в deployment.md.

Основания: [регистрация Twitch application](https://dev.twitch.tv/docs/authentication/register-app/), [Authorization Code Flow](https://dev.twitch.tv/docs/authentication/getting-tokens-oauth/), [проверка токенов](https://dev.twitch.tv/docs/authentication/validate-tokens/).
