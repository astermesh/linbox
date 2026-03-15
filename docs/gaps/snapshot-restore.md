# Gap: Snapshot and Restore

**Severity:** Low (nice-to-have, not blocking)
**Blocks:** Multiverse exploration (Antithesis-style)

## What's Missing

Antithesis's key feature is "multiverse exploration" — snapshot entire VM state, branch, explore multiple futures. LinBox has no equivalent:

- CRIU (Checkpoint/Restore in Userspace) can snapshot Linux processes, but integration is not designed
- Filesystem snapshots (overlay fs, btrfs) for disk state
- Shared memory state capture (controller state, virtual time, PRNG position)
- Restoration of all above atomically

## What's Known

- CRIU is included in the dev environment packages (E01-S06)
- No design work has been done on snapshot/restore
- Planned in S15 (Snapshot/Restore)

## Resolution Path

Investigate CRIU integration as part of S15. Key questions:
1. Can CRIU snapshot a container with active unix sockets?
2. How to coordinate controller state with container state?
3. Is filesystem snapshot + CRIU sufficient, or do we need VM-level snapshots?

---

[← Back to gaps](README.md)
