# ClipStream

Клипы Twitch, единые профили каналов и пользователей, отслеживание авторов, друзья и обсуждения. Авторизация — **только Twitch OAuth**.

`/cliprush` — вертикальная лента с фиксацией на клипе: прокрутка/свайп, стрелки ↑/↓ и Page Up/Down, кнопки перехода, лайки и обсуждение. Активен один Twitch iframe; автозапуск запрашивается без звука, звук включается в плеере. На узком экране, где нельзя соблюсти минимальную ширину Twitch embed 400 px, доступно превью с переходом на Twitch. Автоматического перехода по вымышленному времени окончания нет.

## Локальный запуск

Требуются Docker Desktop с Linux containers и Docker Compose; для генератора секретов — PowerShell 7.

```powershell
Copy-Item .env.example .env
pwsh deploy/init-local.ps1
# Заполнить .env и файл temp/secrets/twitch_client_secret.
docker compose --env-file .env up --build -d
```

Сайт: http://localhost. Наружу опубликован только HTTP на `127.0.0.1:80`; MySQL, Redis и API доступны внутри Docker network. Локальная конфигурация не является кластером высокой доступности.

Для Twitch укажите Client ID в `.env`, Client Secret в отдельном файле секретов и зарегистрируйте точный `TWITCH_REDIRECT_URI` в [Twitch Developer Console](https://dev.twitch.tv/console/apps). Пароли БД, Redis, JWT и gateway proof создаёт `deploy/init-local.ps1`; они не входят в образ и Git. Генератор не перезаписывает существующие секреты.

Документы некоммерческого сервиса встроены в `/legal`: условия, политика, согласие, браузерное хранение и условия рекламного модуля. По вашему решению реквизиты пока оставлены полями для заполнения; они не блокируют технический вход. Перед публичным запуском заполните `LEGAL_OPERATOR_NAME`, `LEGAL_CONTACT_EMAIL` и проверьте документы с учётом юрисдикции. Переменные `LEGAL_*_URL` позволяют заменить встроенную редакцию опубликованными документами. При изменении условий обновляйте `LEGAL_CONSENT_VERSION`.

При **изначально пустой базе** первый подтверждённый вход Twitch-аккаунта `deexsd` получает роль администратора. Другие пользователи могут входить раньше и не получают эту роль. Сервер разрешает login `deexsd` через Helix и сравнивает Twitch ID; локальное имя профиля или email не дают административных прав. Для существующей непустой базы автоматическое назначение отключено. Дальнейшие роли назначаются в `/admin`.

```powershell
Invoke-RestMethod http://localhost/api/health/ready
# Независимые API и frontend replicas на одном Docker host:
docker compose --env-file .env up -d --scale api=2 --scale frontend=2 --scale worker=2
```

## Разработка и проверка

В шапке доступно переключение светлой и тёмной темы; выбор сохраняется в браузере. Кнопка «Языки клипов» открывает фильтр языка речи в клипах Twitch, а не настройку языка интерфейса. В фильтре есть поиск по названию или коду языка и удаление выбранных языков. «Все языки» сбрасывает фильтр.

Светлая палитра распространяется на профиль, комментарии, панели ClipRush и консоль администратора, включая диалоги и выпадающие списки. Контраст текста и границ полей проверяется в `frontend/tests/theme.test.js`. Содержимое встроенного плеера Twitch оформляется самим Twitch.

Frontend: React 18, React Router 7, TanStack Query, Vite 8, CSS. Node.js 24.22.1 используется в Docker. Шрифт Inter поставляется локально. Backend: C++20, Boost.Beast/Asio, MySQL 8, Redis 7.

```powershell
cd frontend
npm ci
npm test
npm run build
npm run dev
```

Сборка frontend записывается в `frontend/build`; backend — в `backend/build`; временные файлы — в `temp` соответствующего проекта. Dev server слушает `127.0.0.1:3000`, проксирует `/api` на локальный edge `127.0.0.1:80`. Для другого edge задайте `API_PROXY_TARGET`.

Backend собирается через `backend/Dockerfile`; CMake включает CTest. DB/Redis integration tests требуют отдельной тестовой среды и не запускаются против production DB. Детали — в [отчёте проверки](docs/validation.md).

- [Архитектура и контракты](docs/architecture.md)
- [Настройка Twitch OAuth](docs/twitch-oauth.md)
- [Документы и некоммерческий запуск](docs/legal-operations.md)
- [Развёртывание и масштабирование](docs/deployment.md)
- [Результаты проверки и ограничения](docs/validation.md)
