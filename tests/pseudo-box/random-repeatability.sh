#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="$ROOT/build"
make -C "$ROOT" build >/dev/null

run_once() {
  local sock shm ctrl_pid out
  sock="/tmp/linbox-pseudobox-rand-$$-$RANDOM.sock"
  shm="/linbox-pseudobox-rand-$$-$RANDOM"

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

  "$BUILD_DIR/linbox-controller" >/tmp/linbox-rand-repeat-$$.log 2>&1 &
  ctrl_pid=$!
  sleep 0.2
  out="$($BUILD_DIR/linbox_shim_random_preload)"
  cleanup_inner
  printf '%s\n' "$out"
}

OUT1="$(run_once)"
OUT2="$(run_once)"

[[ "$OUT1" == "$OUT2" ]] || {
  echo "random repeatability failed" >&2
  diff <(printf '%s\n' "$OUT1") <(printf '%s\n' "$OUT2") || true
  exit 1
}

echo "ok: random repeatability"
