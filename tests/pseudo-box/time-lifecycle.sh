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

export LINBOX_SOCK="$SOCK"
export LINBOX_SHM="$SHM"
export LD_PRELOAD="$BUILD_DIR/liblinbox.so"

"$BUILD_DIR/linbox-controller" >/tmp/linbox-time-lifecycle-$$.log 2>&1 &
CTRL_PID=$!
sleep 0.2

OUT1="$(LC_ALL=C TZ=UTC date '+%Y-%m-%d %H:%M:%S')"
[[ "$OUT1" == "2025-01-01 00:00:00" ]] || {
  echo "expected initial virtual time 2025-01-01 00:00:00, got: $OUT1" >&2
  exit 1
}

python3 - <<'PY'
import os, socket, struct
sock = os.environ['LINBOX_SOCK']
msg = struct.pack('<BBHIqI', 1, 3, 0, 12, 1735689723, 456000000)
with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
    s.connect(sock)
    s.sendall(msg)
    data = s.recv(64)
    if not data or data[1] != 2:
        raise SystemExit('controller did not ACK SET_TIME')
PY

OUT2="$(LC_ALL=C TZ=UTC date '+%Y-%m-%d %H:%M:%S')"
[[ "$OUT2" == "2025-01-01 00:02:03" ]] || {
  echo "expected updated virtual time 2025-01-01 00:02:03, got: $OUT2" >&2
  exit 1
}

echo "ok: time lifecycle"