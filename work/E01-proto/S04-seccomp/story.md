# S04: Seccomp Safety Net

**Epic:** Proto
**Status:** Backlog

## Business Result

No bypass paths. Even programs that make direct syscalls (Go runtime, statically linked binaries) get intercepted. The sandbox has no holes.

## Scope

- seccomp-bpf filter installation
- SIGSYS signal handler with syscall dispatch
- Integration with existing time and random interception handlers

## Tasks

- [T01: seccomp-bpf + SIGSYS handler](T01-seccomp-sigsys/task.md)

## Dependencies

- S01 (shim scaffold)
- S03 (random handlers to integrate with)

## Acceptance Criteria

- Direct `syscall(__NR_clock_gettime, ...)` returns fake time
- Direct `syscall(__NR_getrandom, ...)` returns deterministic output
- Non-intercepted syscalls pass through unaffected
- Normal signal handling is not broken

---

[← Back](../epic.md)
