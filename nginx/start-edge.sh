#!/bin/sh
set -eu
umask 077
PROXY_AUTH=$(cat /run/secrets/proxy_auth)
case "$PROXY_AUTH" in *[!A-Za-z0-9+/=]*) echo 'Invalid proxy secret format' >&2; exit 1;; esac
if [ "${#PROXY_AUTH}" -lt 32 ]; then echo 'Proxy secret is too short' >&2; exit 1; fi
TWITCH_IMAGE_DNS_RESOLVERS=${TWITCH_IMAGE_DNS_RESOLVERS:-127.0.0.11}
case "$TWITCH_IMAGE_DNS_RESOLVERS" in *[!0-9.:\ ]*) echo 'Invalid Twitch image DNS resolver format' >&2; exit 1;; esac
export PROXY_AUTH TWITCH_IMAGE_DNS_RESOLVERS
envsubst '${PROXY_AUTH} ${TWITCH_IMAGE_DNS_RESOLVERS}' < /etc/nginx/nginx.conf.template > /tmp/edge.conf
unset PROXY_AUTH TWITCH_IMAGE_DNS_RESOLVERS
exec nginx -c /tmp/edge.conf -g 'daemon off;' "$@"
