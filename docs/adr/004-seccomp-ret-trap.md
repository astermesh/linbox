# ADR-004: SECCOMP_RET_TRAP for Proto Phase

**Status:** Accepted
**Date:** 2025-05
**Context:** R01-boxing research, E01-S04 design

## Decision

Use `SECCOMP_RET_TRAP` (in-process SIGSYS handler) for the proto phase. Defer `seccomp_unotify` (cross-process supervisor) to Phase 2 for fd-producing syscalls.

## Context

Two seccomp interception modes are relevant:

| Mode | Mechanism | Overhead | Capabilities |
|------|-----------|----------|-------------|
| `SECCOMP_RET_TRAP` | Sends SIGSYS to the calling process | ~hundreds of ns, 0 context switches | Can read/modify registers via ucontext, cannot create fds on behalf of caller |
| `SECCOMP_RET_USER_NOTIF` | Forwards to supervisor process via fd | ~3-7μs, 2 context switches | Can spoof return values, inject fds (Linux 5.9+), full supervisor control |

## Rationale

Proto phase only intercepts time and randomness syscalls — none of which produce file descriptors. `SECCOMP_RET_TRAP` is simpler, faster, and sufficient:

- SIGSYS handler runs in the same process — can directly access the shim's shared memory and PRNG state
- Zero context switches means minimal disruption to the target process
- gVisor's Systrap mode uses exactly this approach at production scale

`seccomp_unotify` becomes necessary in Phase 2 when intercepting `open()`, `socket()`, `accept()` — syscalls that return new file descriptors, which cannot be created across process boundaries without `SECCOMP_IOCTL_NOTIF_ADDFD`.

## Consequences

- SIGSYS handler must correctly extract syscall number and arguments from signal context (`si_syscall`, `ucontext_t->uc_mcontext.gregs`)
- Must set return value via `REG_RAX` in the signal context
- Must not interfere with the application's own signal handling
- Phase 2 migration to `seccomp_unotify` for fd-producing syscalls requires additional controller infrastructure

---

[← Back to ADRs](README.md)
