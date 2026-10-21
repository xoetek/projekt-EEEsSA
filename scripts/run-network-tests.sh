#!/bin/sh
# Run the networking / co-op test suite outside the pi sandbox.
#
# Usage:
#   scripts/run-network-tests.sh
#   BUILD_DIR=build-debug scripts/run-network-tests.sh
#   PORT_BASE=32000 scripts/run-network-tests.sh
#
# Exit codes:
#   0  everything ran and passed
#   1  a real failure
#   77 never returned by this script; network tests that are still sandboxed
#      are reported as SKIP (0 is still returned if every non-skipped test passed)

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT" || exit 1

BUILD_DIR=${BUILD_DIR:-build}
PORT_BASE=${PORT_BASE:-31000}
COOP_PORT=$((PORT_BASE + 1))
DEDICATED_PORT=$((PORT_BASE + 2))
LOG_DIR="$BUILD_DIR/test-logs"

# A normal shell may not have the user-level CMake installed in ~/.pi/agent/bin.
if ! command -v cmake >/dev/null 2>&1; then
    for candidate in "$HOME/.pi/agent/bin" /opt/homebrew/bin /usr/local/bin; do
        if [ -x "$candidate/cmake" ]; then
            PATH="$candidate:$PATH"
            export PATH
            break
        fi
    done
fi

if ! command -v cmake >/dev/null 2>&1; then
    echo "error: cmake not found on PATH"
    echo "       export PATH=\"\$HOME/.pi/agent/bin:\$PATH\""
    exit 1
fi
if ! command -v ctest >/dev/null 2>&1; then
    echo "error: ctest not found on PATH"
    exit 1
fi

mkdir -p "$LOG_DIR" || exit 1

echo "== Building =="
echo "repo:      $ROOT"
echo "build dir: $BUILD_DIR"
echo "logs:      $LOG_DIR"
echo

if ! cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release; then
    echo "error: CMake configure failed"
    exit 1
fi
if ! cmake --build "$BUILD_DIR" -j 8; then
    echo "error: build failed"
    exit 1
fi

PASS=0
FAIL=0
SKIP=0

run_case() {
    name=$1
    shift
    log="$LOG_DIR/$name.log"
    "$@" >"$log" 2>&1
    status=$?
    case "$status" in
        0)
            PASS=$((PASS + 1))
            printf 'PASS  %-22s\n' "$name"
            ;;
        77)
            SKIP=$((SKIP + 1))
            printf 'SKIP  %-22s (network still blocked)\n' "$name"
            ;;
        *)
            FAIL=$((FAIL + 1))
            printf 'FAIL  %-22s (exit %s)\n' "$name" "$status"
            echo "----- $name log -----"
            cat "$log"
            echo "---------------------"
            ;;
    esac
}

echo
echo "== Network / co-op tests =="
run_case net              "$BUILD_DIR"/eee_net_tests
run_case session          "$BUILD_DIR"/eee_session_tests
run_case coop_smoke       ./tests/coop_smoke.sh "$BUILD_DIR"/eee_demo "$COOP_PORT"
run_case dedicated_smoke  ./tests/dedicated_smoke.sh "$BUILD_DIR"/eee_demo "$BUILD_DIR"/eee_server "$DEDICATED_PORT"

echo
echo "== Full CTest suite =="
CTEST_LOG="$LOG_DIR/ctest.log"
ctest --test-dir "$BUILD_DIR" --output-on-failure >"$CTEST_LOG" 2>&1
CTEST_STATUS=$?
tail -n 20 "$CTEST_LOG"
if [ "$CTEST_STATUS" -ne 0 ]; then
    echo "error: CTest failed; full log: $CTEST_LOG"
fi

echo
echo "== Summary =="
printf 'passed: %s\n' "$PASS"
printf 'skipped: %s\n' "$SKIP"
printf 'failed: %s\n' "$FAIL"
printf 'logs:   %s\n' "$LOG_DIR"

if [ "$FAIL" -ne 0 ] || [ "$CTEST_STATUS" -ne 0 ]; then
    exit 1
fi

if [ "$SKIP" -ne 0 ]; then
    echo "One or more network tests still skipped. Run this script outside the sandbox with network bind/connect allowed."
    exit 0
fi

echo "All networking and co-op tests passed."
exit 0
