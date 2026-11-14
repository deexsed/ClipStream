# Развёртывание и масштабирование

## Локальная среда

`docker-compose.yml` запускает MySQL, Redis, одноразовую миграцию, API, worker, статический frontend и Nginx. Только edge публикует loopback HTTP port. `secrets-init` копирует исходные секреты в раздельные named volumes с владельцами runtime UID и правами 0400; edge не получает JWT или пароль БД. Исходные файлы остаются приватными. Повторная генерация не меняет действующие пароли.

```powershell
Copy-Item .env.example .env
pwsh deploy/init-local.ps1
# Заполнить параметры Twitch/документов и файл секрета.
docker compose --env-file .env config --quiet
docker compose --env-file .env up --build -d
Invoke-RestMethod http://localhost/api/health/ready
docker compose --env-file .env logs --tail=100 api worker migrate
```

API запускается только после успешной миграции. MySQL/Redis readiness проверяются до неё. Контейнеры API/frontend/edge работают без root, с read-only filesystem; временные файлы идут в tmpfs. Логи edge исключают query strings и callback, чтобы не сохранять OAuth code. Не включайте отладочный access log с `$request` на callback.

Изменение пароля в файле не меняет пароль существующего MySQL/Redis автоматически. Ротация требует согласованного обновления сервиса и клиентов. `docker compose down` сохраняет volumes. Не используйте `down -v` для рабочей базы.

## Несколько машин

`deploy/docker-stack.yml` — конфигурация Docker Swarm для независимых API, worker, frontend и edge replicas. Минимальная практическая схема: три manager nodes для quorum, replicas распределены по разным узлам. Образы backend/frontend нужно собрать, опубликовать в доступный всем nodes registry и указать **digest**, а не mutable tag.

MySQL и Redis в stack намеренно внешние: нужны настроенные HA endpoints, резервные копии и проверка восстановления. Репликация stateful-сервиса не достигается простым `replicas: 3`. Для MySQL используйте управляемый HA кластер или проверенную схему failover; Redis endpoint должен поддерживать выбранный клиентом обычный TLS connection и сохранять общий namespace квот/leases. Эта поставка не создаёт MySQL cluster, Redis Sentinel/Cluster или внешний балансировщик.

Создайте Swarm secrets из защищённых файлов, не из открытых аргументов shell:

```powershell
docker secret create clipstream_mysql_password temp/secrets/mysql_password
docker secret create clipstream_redis_password temp/secrets/redis_password
docker secret create clipstream_jwt_secret temp/secrets/jwt_secret
docker secret create clipstream_proxy_auth temp/secrets/proxy_auth
docker secret create clipstream_twitch_client_secret temp/secrets/twitch_client_secret
# Аналогично: clipstream_mysql_ca, clipstream_redis_ca, clipstream_tls_cert, clipstream_tls_key.
```

Задайте environment variables, требуемые `${...:?}` в manifest: опубликованные `CLIPSTREAM_BACKEND_IMAGE`/`CLIPSTREAM_FRONTEND_IMAGE`, внешние `MYSQL_HOST`/`REDIS_HOST`, Twitch Client ID и публичный HTTPS callback. Задайте настоящие `LEGAL_*`; пример `.env` не подставляется автоматически Docker Stack CLI — переменные должны быть экспортированы в shell. В stack включены проверка цепочки и hostname TLS для БД/Redis и encrypted overlay.

Сначала запустите миграцию и убедитесь, что job завершился с exit code 0:

```powershell
docker stack config -c deploy/migrate-stack.yml
docker stack deploy -c deploy/migrate-stack.yml clipstream-migrate
docker service ps --no-trunc clipstream-migrate_migrate
docker service logs clipstream-migrate_migrate
# Только после успеха:
docker stack config -c deploy/docker-stack.yml
docker stack deploy -c deploy/docker-stack.yml clipstream
```

`GET_LOCK` сериализует миграции одной БД, checksums запрещают изменение уже применённого SQL. DDL MySQL не является общей транзакцией; частично применённое изменение требует проверки. Перед обновлением обязательна проверенная резервная копия и rehearsal на копии текущей схемы. Не редактируйте применённые миграции: добавляйте следующую версию и обновляйте ожидаемую версию приложения.

Edge использует host-mode publish, чтобы IP не заменялся Swarm ingress SNAT. Направляйте трафик на несколько edge nodes через HA L4 балансировщик, сохраняющий source IP, либо согласованную инфраструктуру real-IP. Произвольным `X-Forwarded-For` приложение не доверяет. Изменение схемы proxy требует отдельной проверки IP bans; иначе можно заблокировать адрес балансировщика вместо клиента. TLS certificate должен покрывать публичный домен; обновление сертификатов также необходимо организовать оператору.

```powershell
docker service scale clipstream_api=6 clipstream_frontend=3
```

Учитывайте `max_replicas_per_node: 1`: для шести API replicas нужны шесть подходящих nodes. Лимит соединений MySQL должен покрывать **все** API/worker pools и migration job. Добавление worker replicas обеспечивает подхват owner lease; одновременно импорт выполняет владелец lease, а не каждая replica. Это не шардирование импорта.

Rolling updates используют healthcheck и rollback policy. `/api/health/live` показывает живой HTTP процесс, `/api/health/ready` проверяет схему, MySQL и Redis. Недоступность Twitch не мешает отдаче уже импортированных публичных данных, но отдельные Twitch-зависимые запросы могут возвращать временные ошибки. Полный uptime зависит от всех внешних HA endpoints, сети и балансировщика; manifest сам по себе не гарантирует SLA.

## Реклама и эксплуатация

Задайте `ADS_MEDIA_HOSTS` списком точных HTTPS hosts, например доверенного CDN оператора. Пустое значение не разрешает media creatives. Проверяйте доступность видео и изображений из браузера, включая CORS/redirect policy CDN. Приложение не загружает произвольные файлы на сервер и не создаёт платёжную инфраструктуру.

Сохраняйте backups MySQL, наблюдайте latency/error rate API, насыщение очереди executor, пул БД, Redis errors, Twitch 429/503, возраст последней синхронизации и worker lease. Общая квота не защищает от запросов других приложений или инструментов, использующих тот же Twitch Client ID вне этой системы.

## Статические assets при rolling update

Для нескольких frontend machines и плавного обновления используйте общий immutable asset origin. Задайте build argument `ASSET_BASE_URL=https://cdn.example.org/clipstream/` (обязателен завершающий `/`); в локальном Compose он берётся из `.env`. Это адрес для JS/CSS/fonts, а маршруты приложения остаются на домене сайта.

Перед обновлением frontend image загрузите `frontend/build/assets/` в соответствующий префикс CDN. Сохраняйте предыдущие content-hashed файлы на срок обслуживания старых вкладок и cache TTL; не перезаписывайте их другим содержимым. CDN должен отдавать правильные MIME types и CORS headers для module scripts/fonts. Приложение генерирует `third-party-notices.txt` с лицензиями production dependencies и Inter — сохраняйте его с релизом.

Без общего хранилища assets старые вкладки после удаления прежнего frontend release могут не загрузить ещё не открытые lazy routes. Поэтому локальное масштабирование контейнеров проверяет репликацию и отказ одного процесса, но не заменяет публикацию и retention assets для production rolling updates. CDN/bucket в этой работе не создавался и внешняя публикация не выполнялась.


## Обновление Swarm configs и secrets

Swarm configs неизменяемы. При изменении `nginx/edge.prod.conf` или `nginx/start-edge.sh` задайте новую `CLIPSTREAM_CONFIG_VERSION` перед `docker stack deploy`, например `v2`: будут созданы новые `clipstream_edge_config_v2` и `clipstream_edge_start_v2`. Для изменения только image digest или числа replicas версию config менять не нужно. Сохраняйте предыдущие configs до завершения rollout и возможности rollback; удаляйте их только после проверки, что services больше их не используют.

Для ротации secrets создайте новые external secret objects и задайте их имена через `CLIPSTREAM_SECRET_MYSQL_PASSWORD`, `CLIPSTREAM_SECRET_MYSQL_CA`, `CLIPSTREAM_SECRET_REDIS_PASSWORD`, `CLIPSTREAM_SECRET_REDIS_CA`, `CLIPSTREAM_SECRET_JWT_SECRET`, `CLIPSTREAM_SECRET_TWITCH_CLIENT_SECRET`, `CLIPSTREAM_SECRET_PROXY_AUTH`, `CLIPSTREAM_SECRET_TLS_CERT`, `CLIPSTREAM_SECRET_TLS_KEY`. Значения этих переменных — имена объектов Swarm, а не содержимое секретов. Общие имена должны совпадать при запуске application stack и migration stack. По умолчанию сохраняются имена `clipstream_*` из примеров выше. Смена credentials требует согласованного обновления внешнего сервиса и клиентов; ротация JWT secret завершает действующие JWT sessions.

Для `start-first` вместе с `max_replicas_per_node: 1` нужен свободный подходящий node и запас ресурсов на временную дополнительную replica. Учитывайте это отдельно от числа nodes для установившейся работы.

Frontend service использует DNS round-robin: edge получает адреса replicas и может повторить GET при timeout/error/404 на другой replica (до трёх попыток в ограниченном окне). Это помогает во время rolling update, но не заменяет retention старых content-hashed assets после удаления прежних replicas.
