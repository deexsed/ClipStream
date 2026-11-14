# syntax=docker/dockerfile:1
FROM ubuntu:22.04@sha256:74b12b11ffa550fba1929ad4d8d16533c34f6612728469671afba1b03b66881c AS dependencies
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends build-essential cmake git ca-certificates libboost-system-dev libssl-dev libmysqlclient-dev libhiredis-dev nlohmann-json3-dev && rm -rf /var/lib/apt/lists/*
ARG HIREDIS_REV=ccad7ebaf99310957004661d1c5f82d2a33ebd10
COPY deploy/hiredis-hostname.patch /app/temp/hiredis-hostname.patch
RUN git init /app/temp/hiredis && git -C /app/temp/hiredis remote add origin https://github.com/redis/hiredis.git && git -C /app/temp/hiredis fetch --depth 1 origin ${HIREDIS_REV} && git -C /app/temp/hiredis checkout --detach FETCH_HEAD && git -C /app/temp/hiredis apply /app/temp/hiredis-hostname.patch && cmake -S /app/temp/hiredis -B /app/temp/hiredis/build -DENABLE_SSL=ON -DDISABLE_TESTS=ON && cmake --build /app/temp/hiredis/build -j2 && cmake --install /app/temp/hiredis/build
ARG REDIS_PLUS_PLUS_REV=a63ac43bf192772910b52e27cd2b42a6098a0071
RUN git init /app/temp/redis-plus-plus && git -C /app/temp/redis-plus-plus remote add origin https://github.com/sewenew/redis-plus-plus.git && git -C /app/temp/redis-plus-plus fetch --depth 1 origin ${REDIS_PLUS_PLUS_REV} && git -C /app/temp/redis-plus-plus checkout --detach FETCH_HEAD && cmake -S /app/temp/redis-plus-plus -B /app/temp/redis-plus-plus/build -DREDIS_PLUS_PLUS_CXX_STANDARD=20 -DREDIS_PLUS_PLUS_BUILD_TEST=OFF -DREDIS_PLUS_PLUS_USE_TLS=ON && cmake --build /app/temp/redis-plus-plus/build -j2 && cmake --install /app/temp/redis-plus-plus/build
RUN ldconfig
FROM dependencies AS builder
WORKDIR /app/backend
COPY backend/ ./
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j2 && ctest --test-dir build --output-on-failure
FROM ubuntu:22.04@sha256:74b12b11ffa550fba1929ad4d8d16533c34f6612728469671afba1b03b66881c
RUN apt-get update && apt-get install -y --no-install-recommends libboost-system1.74.0 libssl3 libmysqlclient21 ca-certificates curl && rm -rf /var/lib/apt/lists/*
COPY --from=builder /usr/local/lib/libredis++* /usr/local/lib/
COPY --from=builder /usr/local/lib/libhiredis* /usr/local/lib/
COPY --from=builder /app/temp/hiredis/COPYING /usr/share/doc/clipstream/hiredis-LICENSE
COPY --from=builder /app/temp/redis-plus-plus/LICENSE /usr/share/doc/clipstream/redis-plus-plus-LICENSE
RUN ldconfig
COPY --from=builder /app/backend/build/clipstream_backend /usr/local/bin/clipstream_backend
COPY database/migrations/ /app/migrations/
ENV MIGRATIONS_DIR=/app/migrations
USER 65532:65532
EXPOSE 8080
CMD ["clipstream_backend"]
