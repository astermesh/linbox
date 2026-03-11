# Gap: FUSE Performance for Databases

**Severity:** Medium
**Blocks:** Phase 3 (filesystem interception)

## What's Missing

FUSE (Filesystem in Userspace) has 2-4x latency overhead on filesystem operations. For database WAL (Write-Ahead Log) writes, this could be a significant bottleneck:

- PostgreSQL WAL writes are fsync-heavy and latency-sensitive
- Redis AOF (Append Only File) has similar characteristics
- Benchmarks needed to quantify actual impact

## What's Decided

- FUSE is planned for `/dev/urandom` interception (low frequency, acceptable overhead)
- Filesystem timestamp control via LD_PRELOAD `stat/fstat` interception (no FUSE needed)
- Full filesystem interception via FUSE is optional (Phase 3)

## Resolution Path

1. Benchmark FUSE overhead with pgbench WAL writes
2. If unacceptable, use LD_PRELOAD `open/read/write` interception for selective filesystem control instead of FUSE
3. FUSE may only be needed for `/dev/urandom` and specific virtual device files

---

[← Back to gaps](README.md)
