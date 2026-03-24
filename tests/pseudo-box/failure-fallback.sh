#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="$ROOT/build"
SOCK="/tmp/linbox-pseudobox-fallback-$$.sock"
SHM="/linbox-pseudobox-fallback-$$"
CTRL_PID=""

cleanup() {
  if [[ -n "${CTRL_PID}" ]] && kill -0 "$CTRL_PID" 2>/dev/null; then
    kill "$CTRL_PID" 2>/dev/null || true
    wait "$CTRL_PID" 2>/dev/null || true
  fi
  rm -f "$SOCK"
}
trap cleanup EXIT

make -C "$ROOT" build >/dev/null

export LINBOX_SOCK="$SOCK"
export LINBOX_SHM="$SHM"
export LD_PRELOAD="$BUILD_DIR/liblinbox.so"

"$BUILD_DIR/linbox-controller" >/tmp/linbox-fallback-$$.log 2>&1 &
CTRL_PID=$!
sleep 0.2

OUT1="$(LC_ALL=C TZ=UTC date '+%Y')"
[[ "$OUT1" == "2025" ]] || {
  echo "expected virtual year 2025 before controller stop, got: $OUT1" >&2
  exit 1
}

kill "$CTRL_PID"
wait "$CTRL_PID" || true
CTRL_PID=""
sleep 0.1

OUT2="$(LC_ALL=C TZ=UTC date '+%Y')"
CURRENT_YEAR="$(LD_PRELOAD= date -u '+%Y')"
[[ "$OUT2" == "$CURRENT_YEAR" ]] || {
  echo "expected fallback to real year $CURRENT_YEAR after controller stop, got: $OUT2" >&2
  exit 1
}

echo "ok: failure fallback"