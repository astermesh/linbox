# S10: Filesystem Interception

**Epic:** Proto
**Status:** Backlog

## Required Reading

- [LD_PRELOAD interception — filesystem functions](../../rnd/boxing/ld-preload.md)
- [LinBox architecture — filesystem section](../../rnd/boxing/linux-sandbox.md)
- [Gap: FUSE performance](../../docs/gaps/fuse-performance.md)

## Business Result

File timestamps reflect virtual time. `stat()` returns virtual mtime/atime/ctime. `fsync`/`fdatasync` behavior is controllable (pass-through, no-op, or delay injection). Selective file operations can be intercepted for path virtualization or fault injection.

## Scope

- Timestamp interception: `stat`/`fstat`/`lstat`/`fstatat`/`statx` — return virtual time for st_atime, st_mtime, st_ctime
- Sync interception: `fsync`/`fdatasync`/`sync`/`syncfs`/`msync` — configurable behavior (pass-through, no-op, delay)
- Selective open/read/write interception for path virtualization (from shared memory policy)
- FUSE mount for `/dev/urandom`, `/dev/random` as alternative to open+read interception

## Tasks

- [T01: Filesystem timestamps + fsync](T01-timestamps-fsync/task.md)

## Dependencies

- S01 (shim scaffold)
- S02 (controller — virtual time for timestamp substitution)

## Acceptance Criteria

- `stat("somefile")` returns st_mtime based on virtual time
- `fstat(fd)` returns virtual timestamps for open files
- `statx()` returns virtual timestamps in stx_mtime, stx_atime, stx_ctime
- `fsync()` with no-op policy returns 0 immediately (libeatmydata pattern)
- `fsync()` with delay policy adds configurable latency
- PostgreSQL WAL writes work correctly under interception (filesystem not broken)

---

[← Back](../epic.md)
