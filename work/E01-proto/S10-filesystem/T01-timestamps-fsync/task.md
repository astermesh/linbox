# T01: Filesystem Timestamps + Fsync

**Story:** [S10: Filesystem Interception](../story.md)
**Status:** Backlog

## Description

Intercept filesystem metadata functions to return virtual timestamps. Intercept sync functions for configurable behavior. This ensures deterministic file timestamps and controllable I/O durability semantics.

## Deliverables

- `src/shim/fs-time.c` — interception of:
  - `stat(path, buf)` — call real stat, then overwrite buf->st_atime/st_mtime/st_ctime with virtual time
  - `fstat(fd, buf)` — same: call real, overwrite timestamps
  - `lstat(path, buf)` — same for symlinks
  - `fstatat(dirfd, path, buf, flags)` — same, relative to directory fd
  - `statx(dirfd, path, flags, mask, statxbuf)` — same for statx struct (stx_atime, stx_mtime, stx_ctime, stx_btime)
- `src/shim/fs-sync.c` — interception of:
  - `fsync(fd)` — check policy: PASSTHROUGH → call real, NOOP → return 0, DELAY → usleep + call real
  - `fdatasync(fd)` — same policy
  - `sync()` — same policy
  - `syncfs(fd)` — same policy
  - `msync(addr, length, flags)` — same policy
  - `sync_file_range(fd, offset, nbytes, flags)` — same policy
- `src/shim/fs-open.c` — selective open/openat interception:
  - Check path against fs policy mapping table in shared memory
  - Support path remapping: if policy maps "/original/path" → "/remapped/path", redirect the open
  - Strip O_SYNC/O_DSYNC flags if fsync policy is NOOP (libeatmydata pattern)
- `src/common/shm-layout.h` — extend with fs policy section:
  - Fsync policy (PASSTHROUGH/NOOP/DELAY + delay_us)
  - Path mapping table (from → to, up to 16 entries)

## Tests

- `stat("testfile")` → st_mtime matches virtual time, not real time
- `fstat(open("testfile"))` → virtual timestamps
- `lstat("symlink")` → virtual timestamps
- `statx()` with STATX_MTIME → virtual timestamps
- Create file at virtual time T1, stat it → mtime = T1
- Controller advances virtual time to T2, stat same file → mtime still T1 (real mtime unchanged, we override with virtual time)
- `fsync()` with PASSTHROUGH → calls real fsync, returns normally
- `fsync()` with NOOP → returns 0 immediately (no real fsync)
- `fsync()` with DELAY(10ms) → takes ~10ms, then returns 0
- `open("file", O_WRONLY | O_SYNC)` with NOOP policy → O_SYNC stripped
- Path mapping: open("/mapped/in") redirected to "/mapped/out"
- PostgreSQL: pgbench runs with fsync PASSTHROUGH — results correct
- PostgreSQL: pgbench runs with fsync NOOP — faster, results still correct (data loss acceptable in simulation)

---

[← Back](../story.md)
