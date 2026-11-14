# Итоговый отчёт проверки ClipStream

Проверка завершена 14 ноября 2026 года. Среда: Windows, Docker Desktop Linux containers, MySQL 8, Redis 7, Node 24.22.1; встроенный браузер. Изменения проверялись на отдельном Compose-проекте `clipstream-check` с синтетическими пользователями и 45 клипами. Это не production-база и не подтверждение входа реального владельца Twitch.

## Результат переработки

| До | После | Причина |
| --- | --- | --- |
| Монолитный запуск HTTP, миграций и фоновой работы | Роли `api`, `worker`, `migrate`, shared MySQL/Redis, отдельные реплики frontend | Независимые старт и масштабирование, отсутствие DDL в старте API |
| Парольный вход и предсказуемый пароль OAuth-аккаунта | Только Twitch OAuth, одноразовый state с привязкой браузера, auth_version | Закрытие обхода авторизации и отзыв выданных сеансов |
| Дубли страниц пользователей и каналов | Единая identity по Twitch ID и одна страница профиля | Сохранение подписчиков и корректное связывание владельца |
| Неявное смешение подписок и дружбы | Раздельные follows ClipStream, read-only follows Twitch, взаимные друзья | Предсказуемые действия и политики приватности |
| Несогласованные стили, задержка появления карточек, слабая мобильная навигация | Общая типографика и компоненты, семантические ссылки, нижняя навигация, native dialog | Клавиатура, доступность и немедленный отклик |
| Пересоздание iframe и вымышленные секунды просмотра | Один iframe на активный клип, ручные переходы, открытие отдельно от Twitch views | Достоверная статистика и стабильность плеера |
| Локальные лимиты Twitch и неполные cursor-списки | Общие Redis бюджеты, backoff, single-flight, batch metadata, продолжение импорта | Несколько API не должны умножать внешний лимит |
| Ограниченная модерация | Временное отстранение, постоянный account/chat ban, IP bans, жалобы, скрытие и классификация, аудит | Серверное применение решений на всех репликах |
| Отсутствие отдельного рекламного контура | Кампании banner/feed/preroll, расписание, caps, proof событий, отдельная админка | Управление инвентарём без подмены пользовательского контента |
| Страница с текстом о будущих документах | Встроенные условия, политика, согласие, browser storage и условия рекламы | Пользователь может прочитать документы до входа |

Применены Flint UI design principles и `apple-design` из указанного репозитория Emil Kowalski: сдержанное движение, немедленный feedback, читаемость, сокращение ненужных эффектов и учёт reduced motion. Тяжёлые 3D-библиотеки для этого интерфейса не потребовались. UI реализован основным агентом; изменения backend и инфраструктуры перепроверены при интеграции.

## Выполненные проверки

| Проверка | Фактический результат |
| --- | --- |
| Frontend Vitest | 31/31 PASS, 7 test files: сеть, session/logout, OAuth callback/remount/state, диалог, профили, реклама, админка, iframe identity, пустая лента, документы и безопасные overrides |
| Backend build + CTest с отдельными MySQL/Redis | 6/6 PASS, 0 skips, 7,34 s; core/database/ads/profile/redis/Twitch contracts |
| Production Docker build | Backend и frontend собраны. В build-stage без внешних DB/Redis 3 integration suites пропущены; полный запуск выше выполнен отдельно |
| Матрица HTTP чтения | 48/48 PASS: guest/auth feeds, сортировки/периоды, home/recommendations/related/trends, profiles, social и admin reads |
| HTTP mutations/permissions | 47/47 PASS: Unicode, pagination, privacy, votes/views boundaries, comments/wall CRUD, ads events/replay/cap, JWT revoke и moderation |
| Между двумя API replicas | 14/14 PASS: общие follows, trusted IP proof, блокировки и рекламный cap |
| Permanent chat ban | 6/6 PASS через production HTTP: explicit flag, отсутствие sentinel expiry, запрет comments/wall, снятие |
| Остановка API replica 1 | 20 параллельных запросов через edge → HTTP 200 от оставшейся replica |
| Остановка frontend replica 1 | 10 параллельных запросов через edge → HTTP 200 от оставшейся replica |
| Миграции | Чистая/повторная/частично обновлённая legacy DB и два конкурентных migrator; checksum и GET_LOCK. Финальная schema 007 применена к стенду |
| Frontend delivery | Обычная сборка и сборка с внешним asset prefix прошли; prefix найден в index.html, затем восстановлена обычная сборка |
| Зависимости/формат | npm audit: 0 vulnerabilities; Prettier check; git diff --check |
| Контейнеры | Две API, две frontend, две worker; API/frontend/DB/Redis healthy. API mounts не содержат dev binary; секреты отделены по сервисам |
| Браузер | Desktop 1280×800; mobile 390×844 и 320 px: без горизонтального переполнения, видимая навигация, фильтры/профили/settings/following/admin/404/legal/logout |

Backend regressions включают конкурентные vote toggles, ограниченное дерево комментариев, cross-clip reply rejection, pool saturation и reconnect, strict ONLY_FULL_GROUP_BY, все основные high-level feeds, shared quotas/leases и обработку ошибок транспорта. Сохранённая интеграционная проверка первоначально выявила SQL ошибки 1055/1064 в home/recommendation paths; исправления включены в финальные тесты. Дополнительно устранены неоднозначные cache keys и запрет общения, истекавший в 2038 году: schema 007 переводит его в постоянный флаг.

При повторном тесте рекламы сначала обнаружена конкурирующая оставленная тестовая кампания. Проверка изолирована на собственный inventory и повторена успешно; выдача другой доступной кампании после исчерпания одной является допустимой логикой, а не обходом её cap.

## Измерения

Для воспроизведения backend contracts соберите builder target `docker build --target builder -f backend/Dockerfile -t clipstream-tests .`. Запустите его `ctest --test-dir /app/backend/build --output-on-failure` в отдельной Docker network с тестовыми MySQL/Redis и защищённым `--env-file` внутри `temp`. Нужны существующая пустая БД `CLIPSTREAM_TEST_DATABASE=clipstream_test` (суффикс `_test` обязателен), `MYSQL_HOST`, `MYSQL_PORT`, `MYSQL_USER`, `MYSQL_PASSWORD`, `MIGRATIONS_DIR=/app/migrations`, `CLIPSTREAM_TEST_REDIS_HOST` и при необходимости `CLIPSTREAM_TEST_REDIS_PORT`/`REDIS_PASSWORD`. Подключите `database/migrations` read-only в `/app/migrations`. Запускайте CTest последовательно: suites изменяют тестовую схему и данные. Никогда не задавайте production DB или Redis. Без test env интеграционные suites пропускаются, что нельзя считать полным прохождением.

- Чистая миграция schema 001–006 на native Docker volume: **189,9 s**. Это измерение до небольшой 007; не выдаётся за время старта всего нового стека. Последовательные ADD DDL объединены: 41 операция в 17 групп без изменения checksum SQL.
- Тёплый старт API при уже готовых DB/Redis: **1,262 s** до readiness, включая Docker CLI и polling. Это не cold start с загрузкой образов.
- Основной frontend JS около **193 kB / 62,5 kB gzip**, shared UI chunk около **51,3 / 18,7 kB**, CSS около **31,7 / 7,8 kB**; route chunks загружаются отдельно. Эти размеры не являются замером FPS, LCP или производительности под массовой нагрузкой.
- Последний Docker build прерывался длительной приостановкой хоста; его wall time не используется как показатель скорости компиляции.

## Закрытие групп исходного аудита

| Находки | Реализованное исправление / предел проверки |
| --- | --- |
| B01, B06, B11 | Twitch-only auth, запрет password endpoints, сохранение legacy данных без локального входа, current-account/JWT checks |
| B02, B14 | Единый migration job, checksums, конечные ожидания, schema readiness и отличимые SQL errors |
| B03, B04, B07, B10, B12 | Ограниченное bulk-чтение, connection lifecycle, transactions/locks, проверки parent/clip, лимиты |
| B05, B15, B16, B18 | TLS chain/hostname, deadlines, bounded executor, отдельные workers и управляемое завершение |
| B08, B09, B13, B17 | Percent decoding, исправленные ranks SQL, idempotent telemetry delta, согласованные hashed cache keys и invalidation |
| U01–U17, U19–U22 | Пересборка UI/state layer, stable iframe, ссылки/клавиатура, mobile navigation, native dialogs, feedback, reduced motion, 404 и обработка ошибок |
| U18 | Полные встроенные тексты. Исключение по указанию владельца: реквизиты оператора остаются полями для заполнения |
| T01–T07 | Общий бюджет Twitch, поддерживаемые EventSub topics, контролируемая синхронизация и batch/cache корректность; deterministic tests |
| T08–T13 | Reconnect handoff/watchdog, refresh/validation, form encoding, cursor follows и login по Twitch ID; deterministic tests |
| T14–T16 | Unknown/current/manual classification разделены, честный статус EventSub, invalidation live profiles; реальный длительный Twitch session не проверен |

## Что нельзя считать подтверждённым

Последующая проверка основного Compose-проекта `clipstream` подтвердила реальный OAuth `deexsd`: HTTP 200 для обмена и authenticated reads, Twitch ID `74192884`, роль `admin`, singleton bootstrap completed_by=1 и запись `bootstrap_admin` в аудите. Default роли новых пользователей — `user`; доступ к role mutation требует действующего admin на сервере. Реальные секреты в исходниках отсутствуют. Длительный live EventSub не проверен. В прежнем изолированном тестовом стенде credentials были пустыми, synthetic JWT применялся только временным loopback harness.

По скриншоту владельца воспроизведён frontend сбой: remount callback после очистки query терял выполняющийся обмен и показывал «Нет данных авторизации», хотя сервер мог уже завершить вход. Регрессионный тест сначала упал с тем же текстом; после сохранения in-flight операции по state/verifier в памяти прошёл. Дополнительно проверены StrictMode/single exchange, чистый callback с подтверждённым сеансом и отказ при несовпадении state. Пустая лента из-за фильтров теперь не показывается как «Клип не найден».

По последующему прямому указанию владельца строгий возрастной фильтр полностью удалён из UI, настроек API и SQL выдачи. Миграция 008 сбрасывает старые значения и default; у deexsd также выставлено 0. Неизвестная классификация больше не скрывает клипы после OAuth. Регрессионный DB-тест проверяет видимость неизвестного клипа даже при вручную установленном legacy-флаге 1, повторную миграцию и сохранение остальных настроек. Проверка языков, ключевых слов и модераторское скрытие остаются отдельными механизмами. Финальная проверка удаления: production images собраны; 31/31 frontend PASS, 6/6 backend suites PASS без skips на отдельных MySQL/Redis (7,71 s). Миграция 008 применена к основной БД; readiness=ready, admin deexsd сохранён. Во встроенном браузере под реальным сеансом deexsd в настройках нет checkbox фильтра, главная вернула 24 карточки клипов. Тестовые контейнеры удалены, основной Compose-проект оставлен запущенным.

Клипы UI fixtures имеют синтетические slugs: ошибка Twitch player на них ожидаема. Проверены границы iframe, навигация и сохранение node при vote, а не воспроизведение этих несуществующих клипов. Реальный preroll video playback и impression проверены отдельно. До ширины 400 px используется внешний Twitch link согласно минимальному размеру embed.

Не выполнены multi-host Swarm deployment, отказ физического узла, восстановление из backup, production CDN publication и нагрузочный тест с реальной аудиторией. Для uptime нужны внешние HA MySQL/Redis и балансировщик; manifests сами не создают HA storage. Native backend builds Windows/macOS не проверялись.

Реквизиты оператора оставлены по прямому указанию владельца. Документы описывают реализованное поведение; юридическая проверка для выбранной страны, retention policy и обработка удаления остаются ответственностью оператора. Рекламная статистика — клиентские события, не antifraud; платежи не реализованы.

Каталог `plans` удалён по запросу владельца. Скриншоты итогового UI и локальные результаты проверки находятся в игнорируемом `temp`; они не содержат реальных пользовательских данных и не являются публичным production окружением.
