FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential cmake git \
    libboost-all-dev libssl-dev \
    libmysqlclient-dev \
    libhiredis-dev \
    nlohmann-json3-dev \
    && rm -rf /var/lib/apt/lists/*

RUN git clone https://github.com/sewenew/redis-plus-plus.git /tmp/redis++ \
    && cd /tmp/redis++ && mkdir build && cd build \
    && cmake .. -DREDIS_PLUS_PLUS_CXX_STANDARD=20 \
    && make -j$(nproc) && make install \
    && ldconfig

WORKDIR /app
COPY . .

RUN rm -rf build && mkdir -p build && cd build \
    && cmake .. -DCMAKE_BUILD_TYPE=Release \
    && make -j$(nproc)

FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    libboost-system1.74.0 libssl3 \
    libmysqlclient21 libhiredis0.14 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /usr/local/lib/libredis++* /usr/local/lib/
RUN ldconfig

COPY --from=builder /app/build/clipstream_backend /usr/local/bin/

EXPOSE 8080

CMD ["clipstream_backend"]
