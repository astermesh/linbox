# S06: Container + E2E

**Epic:** Proto
**Status:** Done

## Required Reading

- [Project structure](../../docs/project-structure.md)
- [LinBox architecture — layered stack](../../rnd/boxing/linux-sandbox.md)
- [ADR-012: Docker runtime](../../docs/adr/012-docker-runtime.md)
- [Development environment](../../docs/env.md)

## Business Result

PostgreSQL in Docker returns `SELECT now()` = controlled time, `SELECT uuid_generate_v4()` is deterministic. The full stack works end-to-end.

## Scope

- Docker image with liblinbox.so
- docker-compose with controller + sandbox container
- seccomp JSON profile
- PostgreSQL integration tests

## Tasks

- [T01: Docker integration](T01-docker/task.md)
- [T02: PostgreSQL E2E](T02-postgresql-e2e/task.md)

## Dependencies

- All previous stories (S01-S05)

## Acceptance Criteria

- `docker compose up` starts controller + PostgreSQL sandbox
- `SELECT now()` returns time set by controller
- `SELECT uuid_generate_v4()` is deterministic across runs

---

[← Back](../epic.md)
