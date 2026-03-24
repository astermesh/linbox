# S02: Controller + SBP Protocol

**Epic:** Proto
**Status:** Done

## Required Reading

- [Project structure](../../docs/project-structure.md)
- [LD_PRELOAD interception — transport section](../../rnd/boxing/ld-preload.md)
- [ADR-002: Shared memory seqlock](../../docs/adr/002-shm-seqlock-time.md)
- [ADR-006: SBP protocol](../../docs/adr/006-sbp-protocol.md)

## Business Result

Time is controlled from outside. A controller process sets "it is now 2025-01-01", and the sandboxed process sees that time. Time can be changed dynamically.

## Scope

- SBP (Sandbox Binary Protocol) wire format definition
- Shared memory layout for hot-path data (virtual time)
- Controller process in C (unix socket server)
- Shim connects to controller on init, reads time from shared memory

## Tasks

- [T01: SBP protocol + shared memory](T01-protocol-shm/task.md)
- [T02: Controller + shim integration](T02-integration/task.md)

## Dependencies

- S01 (time interception shim must exist)

## Acceptance Criteria

- Controller sets time to X → process with .so reports time X
- Controller changes time → process sees new time immediately
- Multiple processes read consistent time from shared memory

---

[← Back](../epic.md)
