# ClipStream — Агрегатор Игровых Хайлайтов

Веб-платформа для просмотра игровых клипов (Twitch Clips) с бесконечным скроллом, фильтрацией, комментариями и системой репутации.

## Стек технологий

| Компонент | Технология |
|-----------|-----------|
| Frontend  | React 18, Tailwind CSS, Zustand, Lucide Icons |
| Backend   | C++ 20 (Boost.Beast, nlohmann/json, jwt-cpp) |
| БД        | MySQL 8.0 |
| Кэш       | Redis 7 |
| DevOps    | Docker, Docker Compose, Nginx |

## Архитектура

```
ClipStream/
├── frontend/          # React SPA (Vite)
│   └── src/
│       ├── api/       # HTTP-клиент
│       ├── components/# UI-компоненты (layout, clips, filters, common)
│       ├── hooks/     # React-хуки (infinite scroll)
│       ├── pages/     # Страницы (Home, Profile)
│       └── store/     # Zustand stores
├── backend/           # C++ REST API
│   ├── include/
│   │   ├── server/        # HTTP-сервер (Boost.Beast)
│   │   ├── controllers/   # Обработка запросов
│   │   ├── services/      # Бизнес-логика
│   │   ├── repositories/  # Доступ к данным (MySQL)
│   │   ├── models/        # Доменные модели
│   │   ├── middleware/     # Auth, CORS
│   │   └── database/      # Пул соединений
│   └── src/               # Реализация
├── database/migrations/   # SQL-миграции
├── nginx/                 # Reverse proxy
└── docker-compose.yml
```

## Быстрый старт

```bash
# 1. Скопировать конфиг
cp .env.example .env
# Отредактировать .env: указать TWITCH_CLIENT_ID, TWITCH_CLIENT_SECRET, JWT_SECRET

# 2. Запустить всё
docker compose up --build -d

# 3. Открыть
# http://localhost
```

## API Endpoints

| Метод  | Путь                         | Описание                    |
|--------|------------------------------|-----------------------------|
| POST   | `/auth/register`             | Регистрация                 |
| POST   | `/auth/login`                | Авторизация                 |
| GET    | `/auth/me`                   | Текущий пользователь        |
| GET    | `/clips`                     | Лента клипов (+ фильтры)   |
| GET    | `/clips/:id`                 | Один клип                   |
| GET    | `/clips/search?q=`           | Поиск                       |
| POST   | `/clips/:id/view`            | Запись просмотра            |
| POST   | `/clips/:id/like`            | Лайк / дизлайк             |
| GET    | `/clips/:clip_id/comments`   | Комментарии к клипу         |
| POST   | `/clips/:clip_id/comments`   | Новый комментарий           |
| PUT    | `/comments/:id`              | Редактирование комментария  |
| DELETE | `/comments/:id`              | Удаление комментария        |
| GET    | `/health`                    | Health check                |

## Разработка (без Docker)

### Backend
```bash
cd backend
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
./clipstream_backend
```

### Frontend
```bash
cd frontend
npm install
npm run dev
```
