#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
source tests/e2e/lib.sh

if ! have_docker; then
  static_pg_assets_check
  echo 'pg-determinism: static checks passed (docker unavailable)'
  exit 0
fi

capture_run() {
  local seed="$1"
  start_pg_stack
  linbox_set_time 1735689600
  linbox_set_seed "$seed"
  local output
  output="$(psql_query "SELECT to_char(date_trunc('second', now() AT TIME ZONE 'UTC'), 'YYYY-MM-DD HH24:MI:SS') || '|' || uuid_generate_v4()::text || '|' || random()::text;")"
  stop_pg_stack >/dev/null
  printf '%s\n' "$output"
}

RUN1="$(capture_run 555)"
RUN2="$(capture_run 555)"
RUN3="$(capture_run 556)"

[[ "$RUN1" == "$RUN2" ]]
[[ "$RUN1" != "$RUN3" ]]

echo 'pg-determinism: ok'
