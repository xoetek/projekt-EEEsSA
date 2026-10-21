#!/bin/sh
set -u

DEMO="$1"
PORT="${2:-29411}"
HOST_LOG=$(mktemp /tmp/eee-coop-host.XXXXXX)
CLIENT_LOG=$(mktemp /tmp/eee-coop-client.XXXXXX)

"$DEMO" --backend=null --headless --host="$PORT" --class=warrior \
    --frames=120 --fps-cap=60 >"$HOST_LOG" 2>&1 &
HOST_PID=$!

sleep 0.5

"$DEMO" --backend=null --headless --join="127.0.0.1:$PORT" --class=archer \
    --frames=120 --fps-cap=60 >"$CLIENT_LOG" 2>&1
CLIENT_STATUS=$?

wait "$HOST_PID"
HOST_STATUS=$?

if grep -q "failed to listen\|Operation not permitted" "$HOST_LOG" "$CLIENT_LOG"; then
    echo "coop smoke skipped: TCP bind unavailable"
    rm -f "$HOST_LOG" "$CLIENT_LOG"
    exit 77
fi

if [ "$HOST_STATUS" -ne 0 ] || [ "$CLIENT_STATUS" -ne 0 ]; then
    echo "co-op smoke failed (host=$HOST_STATUS client=$CLIENT_STATUS)"
    echo "--- host ---"
    cat "$HOST_LOG"
    echo "--- client ---"
    cat "$CLIENT_LOG"
    rm -f "$HOST_LOG" "$CLIENT_LOG"
    exit 1
fi

grep -E "co-op: (host started|joined|.*ticks)" "$HOST_LOG" "$CLIENT_LOG" || true
rm -f "$HOST_LOG" "$CLIENT_LOG"
exit 0
