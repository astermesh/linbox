# S15: Snapshot/Restore

**Epic:** Proto
**Status:** Backlog

## Required Reading

- [Gap: Snapshot/restore](../../docs/gaps/snapshot-restore.md)
- [Deterministic systems — Antithesis multiverse](../../rnd/boxing/deterministic-systems.md)

## Business Result

A running sandbox can be checkpointed and restored. This enables Antithesis-style multiverse exploration — snapshot at an interesting point, branch, explore multiple futures from that state.

## Scope

- CRIU (Checkpoint/Restore in Userspace) integration for process state
- Filesystem snapshot via overlay fs (snapshot the upper layer)
- Controller state snapshot (virtual time, PRNG position, timer queue, registered processes)
- Shared memory state capture and restore
- Coordinated snapshot: controller + all sandbox processes atomically

## Tasks

- [T01: CRIU integration + coordinated snapshot](T01-criu/task.md)

## Dependencies

- S06 (Docker — container runtime)
- S02 (controller — state management)

## Acceptance Criteria

- Running PostgreSQL sandbox checkpointed → all processes frozen, state saved
- Restore from checkpoint → PostgreSQL resumes, connections work, virtual time continues from snapshot point
- Two restores from same checkpoint → divergent execution with different seeds produces different results
- Checkpoint includes: process memory, fd table, virtual time, PRNG state, timer queue
- Checkpoint size reasonable for a minimal PostgreSQL instance (< 500MB)

---

[← Back](../epic.md)
