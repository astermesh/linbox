#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
source tests/e2e/lib.sh

if ! have_docker; then
  static_pg_assets_check
  echo 'pg-time: static checks passed (docker unavailable)'
  exit 0
fi

trap stop_pg_stack EXIT
start_pg_stack

linbox_set_time 1735689600
NOW_UTC="$(psql_query "SELECT to_char(date_trunc('second', now() AT TIME ZONE 'UTC'), 'YYYY-MM-DD HH24:MI:SS');")"
CLOCK_UTC="$(psql_query "SELECT to_char(date_trunc('second', clock_timestamp() AT TIME ZONE 'UTC'), 'YYYY-MM-DD HH24:MI:SS');")"
[[ "$NOW_UTC" == '2025-01-01 00:00:00' ]]
[[ "$CLOCK_UTC" == '2025-01-01 00:00:00' ]]

linbox_set_time 1735693200
NEXT_UTC="$(psql_query "SELECT to_char(date_trunc('second', now() AT TIME ZONE 'UTC'), 'YYYY-MM-DD HH24:MI:SS');")"
[[ "$NEXT_UTC" == '2025-01-01 01:00:00' ]]

echo 'pg-time: ok'
