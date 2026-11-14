# syntax=docker/dockerfile:1
FROM node:24.22.1-bookworm-slim@sha256:be8204d4bd327363357f532187b752ff62759c31bedf98e9e18fc08c9cbd605c AS builder
WORKDIR /app/frontend
COPY frontend/package.json frontend/package-lock.json ./
RUN npm ci
COPY frontend/ ./
ARG ASSET_BASE_URL=/
ENV ASSET_BASE_URL=${ASSET_BASE_URL}
RUN npm run build -- --outDir build
FROM nginx:alpine@sha256:fed7e14be83cbd2661a75cdc5b872d936cf155d46c327084d30b8df19ae88d72
COPY nginx/frontend.conf /etc/nginx/nginx.conf
COPY --from=builder /app/frontend/build/ /usr/share/nginx/html/
USER 101:101
EXPOSE 8080
ENTRYPOINT ["nginx", "-g", "daemon off;"]
