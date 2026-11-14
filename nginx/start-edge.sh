#!/bin/sh
set -eu
umask 077
PROXY_AUTH=$(cat /run/secrets/proxy_auth)
case "$PROXY_AUTH" in *[!A-Za-z0-9+/=]*) echo 'Invalid proxy secret format' >&2; exit 1;; esac
if [ "${#PROXY_AUTH}" -lt 32 ]; then echo 'Proxy secret is too short' >&2; exit 1; fi
export PROXY_AUTH
envsubst '${PROXY_AUTH}' < /etc/nginx/nginx.conf.template > /tmp/edge.conf
unset PROXY_AUTH
exec nginx -c /tmp/edge.conf -g 'daemon off;' "$@"
