#!/bin/sh
set -u

DEMO="$1"
SERVER="$2"
PORT="${3:-29412}"
SERVER_LOG=$(mktemp /tmp/eee-dedicated-server.XXXXXX)
C1_LOG=$(mktemp /tmp/eee-dedicated-c1.XXXXXX)
C2_LOG=$(mktemp /tmp/eee-dedicated-c2.XXXXXX)

"$SERVER" --port="$PORT" --once >"$SERVER_LOG" 2>&1 &
SERVER_PID=$!

sleep 0.3

"$DEMO" --backend=null --headless --join="127.0.0.1:$PORT" --class=warrior \
    --frames=120 --fps-cap=60 >"$C1_LOG" 2>&1 &
C1_PID=$!

sleep 0.2

"$DEMO" --backend=null --headless --join="127.0.0.1:$PORT" --class=archer \
    --frames=120 --fps-cap=60 >"$C2_LOG" 2>&1
C2_STATUS=$?

wait "$C1_PID"
C1_STATUS=$?
wait "$SERVER_PID"
SERVER_STATUS=$?

if grep -q "failed to listen\|Operation not permitted" "$SERVER_LOG" "$C1_LOG" "$C2_LOG"; then
    echo "dedicated smoke skipped: TCP bind unavailable"
    rm -f "$SERVER_LOG" "$C1_LOG" "$C2_LOG"
    exit 77
fi

if [ "$SERVER_STATUS" -ne 0 ] || [ "$C1_STATUS" -ne 0 ] || [ "$C2_STATUS" -ne 0 ] ||
   ! grep -q "checksums validated" "$SERVER_LOG"; then
    echo "dedicated smoke failed (server=$SERVER_STATUS c1=$C1_STATUS c2=$C2_STATUS)"
    echo "--- server ---"; cat "$SERVER_LOG"
    echo "--- client 1 ---"; cat "$C1_LOG"
    echo "--- client 2 ---"; cat "$C2_LOG"
    rm -f "$SERVER_LOG" "$C1_LOG" "$C2_LOG"
    exit 1
fi

grep -E "server: session|co-op: .*ticks|co-op: joined" "$SERVER_LOG" "$C1_LOG" "$C2_LOG" || true
rm -f "$SERVER_LOG" "$C1_LOG" "$C2_LOG"
exit 0
