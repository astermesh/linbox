# ADR-002: Shared Memory with Seqlock for Virtual Time

**Status:** Accepted
**Date:** 2025-05
**Context:** R01-boxing research, E01-S02 design

## Decision

Store virtual time in a shared memory region protected by a seqlock. The shim reads time with zero IPC cost; the controller writes time updates atomically.

## Context

`clock_gettime` is one of the most frequently called syscalls — PostgreSQL and other services call it millions of times per second. Any interception overhead is multiplied enormously.

`struct timespec` is 16 bytes (`tv_sec` + `tv_nsec`), which cannot be read atomically on x86-64 without SSE or `cmpxchg16b`. A torn read could produce an invalid time value.

## Rationale

**Seqlock pattern:**
- Writer increments a sequence counter to odd (writing), writes data, increments to even (stable)
- Reader loads sequence, reads data, checks sequence unchanged and even
- On contention, reader retries — cost is a few extra loads, no syscall or IPC

**Performance:** A single atomic load from shared memory ≈ vDSO call performance. This is the same approach used by Shadow.

## Alternatives Considered

- **Unix socket per time read** — ~1-5μs per read (context switch). Unacceptable for hot path.
- **Atomic 64-bit timestamp** — loses nanosecond precision or requires epoch tricks. More fragile.
- **Lock-free with 128-bit CAS** — requires `cmpxchg16b`, not universally available, complex.
- **Mutex** — contention under high read frequency, potential priority inversion.

## Consequences

- Shared memory region must be created by controller and mapped by shim
- Seqlock implementation must be correct on all target architectures (x86-64 initially)
- Controller is the single writer — no write contention by design
- Multiple reader processes (forked backends) all read the same shared memory

---

[← Back to ADRs](README.md)
