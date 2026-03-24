#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="$ROOT/build"
SOCK="/tmp/linbox-pseudobox-time-$$.sock"
SHM="/linbox-pseudobox-time-$$"
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

env -i LINBOX_SOCK="$SOCK" LINBOX_SHM="$SHM" "$BUILD_DIR/linbox-controller" >/tmp/linbox-time-lifecycle-$$.log 2>&1 &
CTRL_PID=$!
for _ in $(seq 1 40); do
  [[ -S "$SOCK" ]] && break
  sleep 0.05
done
sleep 0.2

OUT1="$(env -i LINBOX_SOCK="$SOCK" LINBOX_SHM="$SHM" LD_PRELOAD="$BUILD_DIR/liblinbox.so" LINBOX_DISABLE_SECCOMP=1 LC_ALL=C TZ=UTC /bin/date '+%Y-%m-%d %H:%M:%S')"
[[ "$OUT1" == "2025-01-01 00:00:00" ]] || {
  echo "expected initial virtual time 2025-01-01 00:00:00, got: $OUT1" >&2
  exit 1
}

echo "ok: time lifecycle"
