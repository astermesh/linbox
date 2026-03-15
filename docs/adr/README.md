# Architecture Decision Records

Decisions made during LinBox design and research.

- [ADR-001: Dual-layer syscall interception](001-dual-layer-interception.md) — LD_PRELOAD as primary, seccomp-BPF as safety net
- [ADR-002: Shared memory with seqlock for virtual time](002-shm-seqlock-time.md) — zero-IPC hot path for clock reads
- [ADR-003: ChaCha20 PRNG for deterministic randomness](003-chacha20-prng.md) — seeded PRNG with per-process streams
- [ADR-004: SECCOMP_RET_TRAP for proto phase](004-seccomp-ret-trap.md) — in-process SIGSYS handler, seccomp_unotify deferred
- [ADR-005: Block io_uring via seccomp](005-block-io-uring.md) — prevent uninterceptable submission bypass
- [ADR-006: SBP protocol over Unix socket](006-sbp-protocol.md) — binary protocol for controller-shim communication
- [ADR-007: Linux namespaces for isolation](007-namespaces-isolation.md) — PID, network, mount, user namespaces as Layer 1
- [ADR-008: CPUTIME derived from virtual wall time](008-cputime-model.md) — single-core simulation semantics
- [ADR-009: C for the shim library](009-c-language-shim.md) — language choice for liblinbox.so
- [ADR-010: Network simulation via tc/netem](010-tc-netem-network.md) — kernel-level network emulation
- [ADR-011: Graceful degradation](011-graceful-degradation.md) — fallback to real time when controller unavailable
- [ADR-012: Docker as container runtime](012-docker-runtime.md) — container orchestration for proto phase

---

[← Back to docs](../README.md)
