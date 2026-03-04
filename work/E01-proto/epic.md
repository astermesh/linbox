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

## Non-Goals

- Network latency simulation (Phase 2)
- Timer virtualization — nanosleep, timerfd (Phase 2)
- Filesystem timestamp control (Phase 3)
- eBPF observability (Phase 3)
- Snapshot/restore (Phase 4)
- ARM64 support (Phase 4)

## Stories

| # | Story | Business result |
|---|-------|-----------------|
| S01 | [Time interception](S01-time-interception/story.md) | Process sees fake time |
| S02 | [Controller + SBP protocol](S02-controller-sbp/story.md) | Time controlled by controller |
| S03 | [Random interception](S03-random/story.md) | Deterministic randomness |
| S04 | [Seccomp safety net](S04-seccomp/story.md) | No bypass paths |
| S05 | [Multi-process support](S05-multiprocess/story.md) | Fork-based services work |
| S06 | [Container + E2E](S06-container-e2e/story.md) | PostgreSQL in Docker under control |

## Dependencies

```
S01 ──→ S02 ──→ S05 ──→ S06
  │                       ↑
  ├──→ S03 ───────────────┤
  │                       │
  └──→ S04 ───────────────┘
```

---

[← Back](../README.md)
