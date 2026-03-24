#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

TESTS=(
  "$ROOT/tests/pseudo-box/time-lifecycle.sh"
  "$ROOT/tests/pseudo-box/failure-fallback.sh"
  "$ROOT/tests/pseudo-box/repeatability.sh"
  "$ROOT/tests/pseudo-box/random-repeatability.sh"
)

for test_script in "${TESTS[@]}"; do
  echo "==> $(basename "$test_script")"
  "$test_script"
done

echo "all pseudo-box tests passed"
