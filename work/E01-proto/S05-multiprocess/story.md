# S05: Multi-Process Support

**Epic:** Proto
**Status:** Backlog

## Business Result

Fork-based services (PostgreSQL with per-backend forks) work correctly under LinBox. Every forked process is under control with its own PRNG stream but shared virtual time.

## Scope

- `fork()`, `vfork()`, `clone()`, `clone3()` interception
- Per-process state reinitialization on fork
- `execve()` and exec family (`execvp`, `execvpe`, `execl`, `execle`, `execlp`) — verify LD_PRELOAD inheritance
- `posix_spawn()`, `posix_spawnp()` — verify LD_PRELOAD inheritance
- Child process registration with controller

## Tasks

- [T01: Fork/clone interception](T01-fork-clone/task.md)

## Dependencies

- S02 (controller — child must register)
- S03 (random — child needs new PRNG stream)

## Acceptance Criteria

- Parent and child see same controlled time
- Parent and child get different random streams
- Deep fork chains (3+ levels) all under control
- execve'd binary gets shim loaded

---

[← Back](../epic.md)
