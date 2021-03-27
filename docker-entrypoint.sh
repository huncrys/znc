#!/bin/sh

if [ "${1:0:1}" != '-' ]; then
    exec "$@"
fi

DATADIR="/znc-data"

if [ "$(id -u)" = '0' ]; then
    chown -R znc:znc "$DATADIR" || exit 1
    chmod 700 "$DATADIR" || exit 2
    exec su-exec znc:znc /entrypoint.sh "$@"
fi

{ /usr/sbin/oidentd -i -P `route | awk '/^default\s+/ { print $2 }'` &> "$DATADIR/.oidentd.log" & } || exit 33
znc --foreground --datadir "$DATADIR" "$@"
