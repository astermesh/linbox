#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="$ROOT/build"
make -C "$ROOT" build >/dev/null

run_once() {
  local sock shm ctrl_pid out
  sock="/tmp/linbox-pseudobox-repeat-$$-$RANDOM.sock"
  shm="/linbox-pseudobox-repeat-$$-$RANDOM"

  cleanup_inner() {
    if [[ -n "${ctrl_pid:-}" ]] && kill -0 "$ctrl_pid" 2>/dev/null; then
      kill "$ctrl_pid" 2>/dev/null || true
      wait "$ctrl_pid" 2>/dev/null || true
    fi
    rm -f "$sock"
  }

  export LINBOX_SOCK="$sock"
  export LINBOX_SHM="$shm"
  export LD_PRELOAD="$BUILD_DIR/liblinbox.so"

  "$BUILD_DIR/linbox-controller" >/tmp/linbox-repeat-$$.log 2>&1 &
  ctrl_pid=$!
  sleep 0.2
  out="$(LC_ALL=C TZ=UTC date '+%Y-%m-%d %H:%M:%S')"
  cleanup_inner
  printf '%s\n' "$out"
}

OUT1="$(run_once)"
OUT2="$(run_once)"
OUT3="$(run_once)"

[[ "$OUT1" == "2025-01-01 00:00:00" ]] || {
  echo "unexpected first output: $OUT1" >&2
  exit 1
}
[[ "$OUT1" == "$OUT2" && "$OUT2" == "$OUT3" ]] || {
  echo "repeatability failed: $OUT1 / $OUT2 / $OUT3" >&2
  exit 1
}

echo "ok: repeatability"