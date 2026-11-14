#!/bin/sh
set -eu
umask 077
install_secrets() {
    directory=$1
    owner=$2
    shift 2
    mkdir -p "$directory"
    chown "$owner" "$directory"
    chmod 0700 "$directory"
    for name in "$@"; do
        cp "/run/secrets/$name" "$directory/$name"
        chown "$owner" "$directory/$name"
        chmod 0400 "$directory/$name"
    done
}
install_secrets /app-secrets 65532:65532 mysql_password redis_password jwt_secret twitch_client_secret proxy_auth
install_secrets /db-secrets 999:999 mysql_password mysql_root_password
install_secrets /redis-secrets 999:1000 redis_password
install_secrets /edge-secrets 101:101 proxy_auth
