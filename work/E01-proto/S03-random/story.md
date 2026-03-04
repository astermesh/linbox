# S03: Random Interception

**Epic:** Proto
**Status:** Backlog

## Business Result

A process produces deterministic random output given the same seed. Two runs with identical seed yield byte-for-byte identical random data.

## Scope

- Seeded PRNG (ChaCha20)
- `getrandom()` interception
- `/dev/urandom` and `/dev/random` interception via `open()` + `read()`
- Seed management via controller (SBP/shared memory)

## Tasks

- [T01: Deterministic random](T01-deterministic-random/task.md)

## Dependencies

- S01 (shim scaffold)
- S02 (controller for seed management)

## Acceptance Criteria

- Two runs with same seed produce identical getrandom() output
- `/dev/urandom` reads are deterministic
- Different seeds produce different output

---

[← Back](../epic.md)
