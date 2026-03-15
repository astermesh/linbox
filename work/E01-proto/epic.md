# E01: Proto

**Status:** Backlog

## Goal

Prove that LinBox works — a real unmodified service (PostgreSQL) runs inside a container where time and randomness are fully controlled from outside, with no bypass paths.

## Success Criteria

- PostgreSQL `SELECT now()` returns time set by controller
- PostgreSQL `SELECT uuid_generate_v4()` is deterministic given same seed
- Two identical runs (same seed, same queries) produce byte-for-byte identical results
- Direct syscalls (bypassing libc) are also intercepted — no holes
- Fork-per-backend (PostgreSQL architecture) works correctly under interception
- Performance overhead < 5% for typical query workloads

## Stories

| # | Story | Business result |
|---|-------|-----------------|
| S01 | [Time interception](S01-time-interception/story.md) | Process sees fake time |
| S02 | [Controller + SBP protocol](S02-controller-sbp/story.md) | Time controlled by controller |
| S03 | [Random interception](S03-random/story.md) | Deterministic randomness |
| S04 | [Seccomp safety net](S04-seccomp/story.md) | No bypass paths |
| S05 | [Multi-process support](S05-multiprocess/story.md) | Fork-based services work |
| S06 | [Container + E2E](S06-container-e2e/story.md) | PostgreSQL in Docker under control |
| S07 | [Timer virtualization](S07-timers/story.md) | Sleeps and timers use virtual time |
| S08 | [Network interception](S08-network/story.md) | All network syscalls interceptable |
| S09 | [Network simulation](S09-netem/story.md) | Latency, loss, partitions between boxes |
| S10 | [Filesystem interception](S10-filesystem/story.md) | Virtual timestamps, controllable fsync |
| S11 | [Signal management](S11-signals/story.md) | Timer signals, handler coexistence |
| S12 | [seccomp_unotify supervisor](S12-unotify/story.md) | Fd-producing syscalls from Go/static binaries |
| S13 | [Miscellaneous intercepts](S13-misc/story.md) | Virtual hostname, uptime, resource usage |
| S14 | [Protocol-level interception](S14-protocol/story.md) | Wire protocol observability (Postgres, Redis) |
| S15 | [Snapshot/restore](S15-snapshot/story.md) | Checkpoint and multiverse exploration |
| S16 | [eBPF observability](S16-observability/story.md) | Syscall monitoring and metrics |
| S17 | [ARM64 support](S17-arm64/story.md) | Multi-architecture |

## Dependencies

Core (time + random + safety net + PostgreSQL E2E):
```
S01 ──→ S02 ──→ S05 ──→ S06
  │       │       ↑       ↑
  │       ↓       │       │
  ├──→ S03 ──────┘───────┤
  │       │               │
  │       ↓               │
  └──→ S04 ──────────────┘
```

Extended (timers, network, filesystem, signals):
```
S02 ──→ S07 (timers)
S01, S02 ──→ S08 (network) ──→ S09 (netem)
S01, S02 ──→ S10 (filesystem)
S04, S07 ──→ S11 (signals)
S04, S02 ──→ S12 (unotify)
S01, S02 ──→ S13 (misc)
```

Infrastructure and observability:
```
S06, S08, S09 ──→ S14 (protocol)
S06, S02 ──→ S15 (snapshot)
S06, S04 ──→ S16 (observability)
S01, S04 ──→ S17 (ARM64)
```

---

[← Back](../README.md)
