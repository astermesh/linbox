# ADR-001: Dual-Layer Syscall Interception

**Status:** Accepted
**Date:** 2025-05
**Context:** R01-boxing research

## Decision

Use LD_PRELOAD as the primary interception mechanism and seccomp-BPF as a safety net.

## Context

LinBox needs to intercept ~94 outbound boundary interface (OBI) points — syscalls touching time, randomness, network, filesystem, processes, and signals. No single mechanism covers all cases:

- **LD_PRELOAD** intercepts libc wrappers (~100ns overhead) but misses Go runtime direct syscalls, static binaries, and inline `syscall` instructions.
- **seccomp-BPF** catches all syscalls at kernel level (~800ns with SECCOMP_RET_TRAP) but cannot inspect pointer arguments or modify return values without additional mechanisms.
- **ptrace** can do everything but has ~5-9μs overhead per syscall — unacceptable for hot paths.

## Rationale

The dual-layer approach is proven in production by Shadow (USENIX ATC '22 Best Paper), which uses exactly this pattern: LD_PRELOAD shim for the fast path, seccomp-BPF to catch anything that escapes.

**Performance tiers:**
| Path | Overhead | When used |
|------|----------|-----------|
| LD_PRELOAD + shared memory | ~100ns | 99%+ of calls (libc users) |
| seccomp SECCOMP_RET_TRAP + SIGSYS | ~800ns | Direct syscalls, Go, static binaries |
| seccomp_unotify (Phase 2) | ~3-7μs | Complex fd-producing syscalls |

**Effective coverage:** ~99.9% of real-world service workloads.

## Alternatives Considered

- **LD_PRELOAD only** — misses Go, static binaries, direct syscalls. Unacceptable for "no bypass" guarantee.
- **seccomp-BPF only** — cannot modify return values without ptrace/SIGSYS, expensive for hot paths like clock_gettime (called millions of times).
- **ptrace only** — 5-9μs per syscall, 10-100x slowdown. Used by Hermit (Meta), which is now in maintenance mode partly due to performance.
- **gVisor (full user-space kernel)** — reimplements 274 syscalls, ~800ns overhead. Too heavy for LinBox's goals; we want to run real Linux, not re-implement it.
- **Custom hypervisor (Antithesis approach)** — complete determinism including kernel, but requires VM per container. Overkill for Phase 1.

## Consequences

- Must implement both LD_PRELOAD shim and seccomp filter installation
- Must handle the case where both layers intercept the same call (prevent double interception)
- Go programs and static binaries work but at ~8x higher per-call overhead
- io_uring must be blocked separately (submissions bypass both layers)

---

[← Back to ADRs](README.md)
