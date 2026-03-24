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

  env -i LINBOX_SOCK="$sock" LINBOX_SHM="$shm" "$BUILD_DIR/linbox-controller" >/tmp/linbox-rand-repeat-$$.log 2>&1 &
  ctrl_pid=$!
  for _ in $(seq 1 40); do
    [[ -S "$sock" ]] && break
    sleep 0.05
  done
  sleep 0.2
  out="$(env -i LINBOX_SOCK="$sock" LINBOX_SHM="$shm" LD_PRELOAD="$BUILD_DIR/liblinbox.so" LINBOX_DISABLE_SECCOMP=1 "$BUILD_DIR/linbox_shim_random_preload")"
  cleanup_inner
  printf '%s\n' "$out"
}

OUT1="$(run_once)"
OUT2="$(run_once)"

[[ -n "$OUT1" && -n "$OUT2" ]] || {
  echo "random preload smoke test failed" >&2
  exit 1
}

echo "ok: random preload smoke"
