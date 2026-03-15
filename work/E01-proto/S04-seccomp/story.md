# S04: Seccomp Safety Net

**Epic:** Proto
**Status:** Backlog

## Required Reading

- [Seccomp-BPF and ptrace](../../rnd/boxing/seccomp-ptrace.md)
- [LinBox architecture — edge cases](../../rnd/boxing/linux-sandbox.md)
- [ADR-004: SECCOMP_RET_TRAP](../../docs/adr/004-seccomp-ret-trap.md)
- [ADR-005: Block io_uring](../../docs/adr/005-block-io-uring.md)

## Business Result

No bypass paths. Even programs that make direct syscalls (Go runtime, statically linked binaries) get intercepted. The sandbox has no holes.

## Scope

- seccomp-bpf filter installation
- SIGSYS signal handler with syscall dispatch
- Integration with existing time and random interception handlers

**Architecture note:** Proto uses `SECCOMP_RET_TRAP` (in-process SIGSYS handler, ~hundreds of ns, 0 context switches). `seccomp_unotify` (cross-process supervisor, ~3-7μs) is deferred to Phase 2 for fd-producing syscalls (`open`, `socket`, `accept`) that require supervisor-side fd injection.

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
