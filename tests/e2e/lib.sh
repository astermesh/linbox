#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
COMPOSE_FILE="$ROOT/docker-compose.yml"
LINBOXCTL="$ROOT/tests/e2e/linboxctl.py"

have_docker() {
  command -v docker >/dev/null 2>&1
}

compose() {
  docker compose -f "$COMPOSE_FILE" "$@"
}

static_pg_assets_check() {
  python3 <<'PY'
from pathlib import Path

compose = Path('docker-compose.yml').read_text()
dockerfile = Path('docker/Dockerfile.pg-sandbox').read_text()
entrypoint = Path('docker/pg/entrypoint.sh').read_text()
initdb = Path('docker/pg/initdb.sql').read_text()
linboxctl = Path('tests/e2e/linboxctl.py').read_text()

required_compose_snippets = (
    'pg-sandbox:',
    'docker/Dockerfile.pg-sandbox',
    'POSTGRES_PASSWORD: postgres',
    'POSTGRES_DB: linbox',
    'LINBOX_SOCK: /run/linbox/linbox.sock',
    'seccomp=./docker/seccomp-profile.json',
    'linbox-run:/run/linbox',
    'healthcheck:',
)
for snippet in required_compose_snippets:
    assert snippet in compose, snippet

assert 'FROM postgres:17-bookworm' in dockerfile
assert 'postgresql-contrib' in dockerfile
assert 'liblinbox.so' in dockerfile
assert 'docker-entrypoint.sh' in entrypoint
assert 'uuid-ossp' in initdb
assert '.:/workspace:ro' in compose
assert 'socket.AF_UNIX' in linboxctl
assert 'SET_TIME' in linboxctl
assert 'SET_SEED' in linboxctl
PY
}

wait_for_controller() {
  local timeout="${1:-60}"
  local elapsed=0
  while (( elapsed < timeout )); do
    if compose exec -T controller sh -lc 'test -S /run/linbox/linbox.sock'; then
      return 0
    fi
    sleep 1
    elapsed=$((elapsed + 1))
  done
  echo 'controller socket did not become ready' >&2
  return 1
}

wait_for_postgres() {
  local timeout="${1:-60}"
  local elapsed=0
  while (( elapsed < timeout )); do
    if compose exec -T pg-sandbox sh -lc 'pg_isready -U postgres -d linbox >/dev/null'; then
      return 0
    fi
    sleep 1
    elapsed=$((elapsed + 1))
  done
  echo 'postgres did not become ready' >&2
  return 1
}

psql_query() {
  local sql="$1"
  compose exec -T pg-sandbox sh -lc \
    "PGPASSWORD=postgres psql -v ON_ERROR_STOP=1 -U postgres -d linbox -Atqc \"$sql\""
}

linbox_set_time() {
  local tv_sec="$1"
  local tv_nsec="${2:-0}"
  compose exec -T controller sh -lc \
    "python3 /workspace/tests/e2e/linboxctl.py --socket /run/linbox/linbox.sock set-time --seconds $tv_sec --nanos $tv_nsec"
}

linbox_set_seed() {
  local seed="$1"
  compose exec -T controller sh -lc \
    "python3 /workspace/tests/e2e/linboxctl.py --socket /run/linbox/linbox.sock set-seed --seed $seed"
}

start_pg_stack() {
  compose down -v --remove-orphans >/dev/null 2>&1 || true
  compose up -d --build controller pg-sandbox
  wait_for_controller
  wait_for_postgres
}

stop_pg_stack() {
  compose down -v --remove-orphans
}
