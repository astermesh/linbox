#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

./tests/e2e/docker-smoke.sh
./tests/e2e/pg-time.sh
./tests/e2e/pg-random.sh
./tests/e2e/pg-determinism.sh
