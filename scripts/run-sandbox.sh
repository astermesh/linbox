#!/usr/bin/env bash
set -euo pipefail

export LD_PRELOAD="${LD_PRELOAD:-/usr/local/lib/liblinbox.so}"
export LINBOX_SOCK="${LINBOX_SOCK:-/run/linbox/linbox.sock}"
export LINBOX_SHM="${LINBOX_SHM:-/linbox-shm}"

mkdir -p "$(dirname "$LINBOX_SOCK")"

exec "$@"
