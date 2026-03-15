# Known Gaps

Open questions and unresolved design areas identified during research.

- [SBP protocol specification](sbp-protocol-spec.md) — wire format not yet defined
- [Virtual timer queue](virtual-timer-queue.md) — design for nanosleep, timerfd, alarm virtualization
- [Multi-process time consistency](multi-process-time-consistency.md) — consistent time across PostgreSQL backends
- [Thread scheduling determinism](thread-scheduling-determinism.md) — non-deterministic thread interleaving
- [FUSE performance for databases](fuse-performance.md) — potential bottleneck for WAL writes
- [Snapshot and restore](snapshot-restore.md) — CRIU-based multiverse exploration
- [RDTSC and hardware instructions](rdtsc-hardware.md) — uninterceptable CPU instructions
- [macOS development support](macos-dev-support.md) — cross-platform development environment
- [ARM64 architecture](arm64-support.md) — multi-architecture support

---

[← Back to docs](../README.md)
