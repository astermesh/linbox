#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

required_files=(
  docker/Dockerfile.controller
  docker/Dockerfile.sandbox
  docker/seccomp-profile.json
  docker-compose.yml
  scripts/run-sandbox.sh
)

for path in "${required_files[@]}"; do
  if [[ ! -f "$path" ]]; then
    echo "missing required asset: $path" >&2
    exit 1
  fi
done

if [[ ! -x scripts/run-sandbox.sh ]]; then
  echo "scripts/run-sandbox.sh must be executable" >&2
  exit 1
fi

python3 <<'PY'
import json
from pathlib import Path

profile = json.loads(Path('docker/seccomp-profile.json').read_text())
assert profile['defaultAction'] == 'SCMP_ACT_ALLOW'
actions = {}
for entry in profile['syscalls']:
    for name in entry['names']:
        actions[name] = entry['action']
for syscall in ('io_uring_setup', 'io_uring_enter', 'io_uring_register'):
    assert actions.get(syscall) == 'SCMP_ACT_ERRNO', syscall
for syscall in ('clock_gettime', 'gettimeofday', 'time', 'getrandom'):
    assert actions.get(syscall) == 'SCMP_ACT_TRAP', syscall
PY

if command -v docker >/dev/null 2>&1; then
  docker compose -f docker-compose.yml config >/dev/null
else
  python3 <<'PY'
from pathlib import Path

compose = Path('docker-compose.yml').read_text()
for snippet in (
    'controller:',
    'sandbox:',
    'docker/Dockerfile.controller',
    'docker/Dockerfile.sandbox',
    'seccomp=./docker/seccomp-profile.json',
    'linbox-run:/run/linbox',
):
    assert snippet in compose, snippet
PY
fi

echo "docker smoke checks passed"
