# ADR-005: Block io_uring via Seccomp

**Status:** Accepted
**Date:** 2025-05
**Context:** R01-boxing research

## Decision

Block `io_uring_setup`, `io_uring_enter`, and `io_uring_register` syscalls via `SECCOMP_RET_ERRNO(ENOSYS)`.

## Context

io_uring uses shared memory rings between userspace and kernel. Once the ring is set up, submission queue entries (SQEs) are written directly to the ring buffer — no syscall occurs. This means:

- LD_PRELOAD cannot intercept individual operations
- seccomp-BPF only sees `io_uring_enter` (batch submission), not individual operations
- Operations within the ring (read, write, connect, etc.) are invisible to all userspace interception

## Rationale

Blocking io_uring at setup time is the simplest and most complete solution:

- `ENOSYS` makes the application think the kernel doesn't support io_uring
- Well-written applications fall back to `epoll`/`poll`/`select` + regular syscalls
- PostgreSQL, Redis, Nginx, Node.js all work without io_uring (it's an optimization, not a requirement)
- This is the same approach used by other sandboxing systems

## Alternatives Considered

- **Intercept io_uring at ring level** — would require patching the shared memory ring or running a proxy. Extremely complex, fragile, and would likely break ABI guarantees.
- **Allow io_uring and accept the bypass** — defeats the "no bypass paths" goal.
- **Custom kernel module** — too invasive and not portable.

## Consequences

- Applications that require io_uring (none in our target set) cannot run in LinBox
- Slight performance regression for applications that would benefit from io_uring (they fall back to epoll)
- Clean solution — no complex interception logic needed

---

[← Back to ADRs](README.md)
