#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
source tests/e2e/lib.sh

if ! have_docker; then
  static_pg_assets_check
  echo 'pg-random: static checks passed (docker unavailable)'
  exit 0
fi

run_case() {
  local seed="$1"
  start_pg_stack
  linbox_set_seed "$seed"
  local uuid random_value
  uuid="$(psql_query 'SELECT uuid_generate_v4();')"
  linbox_set_seed "$seed"
  random_value="$(psql_query 'SELECT random();')"
  stop_pg_stack >/dev/null
  printf '%s|%s\n' "$uuid" "$random_value"
}

A="$(run_case 111)"
B="$(run_case 111)"
C="$(run_case 222)"

[[ "$A" == "$B" ]]
[[ "$A" != "$C" ]]

echo 'pg-random: ok'
