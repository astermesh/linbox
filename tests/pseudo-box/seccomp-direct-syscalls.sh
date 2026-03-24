#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${ROOT}/build"
SOCK="/tmp/linbox-seccomp-$$.sock"
SHM="/linbox-seccomp-$$"
CTRL_PID=""
cleanup() {
  if [[ -n "$CTRL_PID" ]]; then
    kill "$CTRL_PID" 2>/dev/null || true
    wait "$CTRL_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT

export LINBOX_SOCK="$SOCK"
export LINBOX_SHM="$SHM"
export LD_PRELOAD="${BUILD_DIR}/liblinbox.so"

env LD_PRELOAD= "${BUILD_DIR}/linbox-controller" &
CTRL_PID=$!
sleep 0.2

"${BUILD_DIR}/linbox_seccomp_helper"
