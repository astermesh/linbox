# T02: PostgreSQL E2E

**Story:** [S06: Container + E2E](../story.md)
**Status:** Done

## Description

Full end-to-end validation with PostgreSQL. Prove that a real, unmodified database service runs under LinBox with fully controlled time and deterministic randomness.

## Deliverables

- `docker/Dockerfile.pg-sandbox` — PostgreSQL image with liblinbox.so
- `docker/pg/entrypoint.sh` — PostgreSQL entrypoint wrapper that preserves LinBox env
- `docker/pg/initdb.sql` — enables the `uuid-ossp` extension at init time
- `tests/e2e/linboxctl.py` — SBP control helper for setting virtual time and seed during E2E runs
- `tests/e2e/pg-time.sh` — E2E test script for time control
- `tests/e2e/pg-random.sh` — E2E test script for deterministic random
- `tests/e2e/pg-determinism.sh` — full determinism validation (two identical runs)

## Tests

- Controller sets time to 2025-01-01 → `SELECT now()` returns `2025-01-01 00:00:00+00`
- Controller advances time by 1 hour → next `SELECT now()` returns `2025-01-01 01:00:00+00`
- `SELECT clock_timestamp()` also returns controlled time
- `SELECT uuid_generate_v4()` with seed X → returns specific UUID
- Same seed X on second run → same UUID
- Different seed Y → different UUID
- `SELECT random()` is deterministic with same seed
- Two full runs (compose up → run queries → compose down) with same seed → byte-for-byte identical query results
- PostgreSQL WAL writes succeed (filesystem not broken by interception)
- PostgreSQL handles concurrent connections (fork-per-backend works)
- `pg_sleep(1)` — document observed behavior (expected: real 1s delay, since timer virtualization is Phase 2 non-goal)
- Performance: run pgbench (scale=1, clients=4, transactions=1000) with and without liblinbox.so, compare TPS — verify overhead < 5% (epic success criterion)

## Verification

- Unit tests: existing shared/shim tests via `make test`
- Preload tests: existing shim preload coverage still applies via `make test`
- Pseudo-box tests: existing controller/shim repeatability coverage still applies via `make pseudo-test`
- E2E tests: `tests/e2e/docker-smoke.sh`, `tests/e2e/pg-time.sh`, `tests/e2e/pg-random.sh`, `tests/e2e/pg-determinism.sh` (all via `make verify`; scripts degrade to static asset validation when Docker is unavailable)
- Manual checks: `docker compose up`, run `psql`, confirm time/random behavior when Docker is available

---

[← Back](../story.md)
