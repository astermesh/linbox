# T01: CRIU Integration + Coordinated Snapshot

**Story:** [S15: Snapshot/Restore](../story.md)
**Status:** Backlog

## Description

Integrate CRIU for checkpointing sandbox processes. Coordinate with the controller to capture a consistent snapshot of the entire simulation state (processes + controller state + filesystem).

## Deliverables

- `src/controller/snapshot.c` — snapshot coordinator:
  - Pause all sandbox processes (freeze via cgroup freezer or SIGSTOP)
  - Capture controller state: virtual time, PRNG positions per process, timer queue, registered processes, network policies
  - Serialize controller state to JSON or binary file
  - Invoke CRIU dump for each sandbox container
  - Snapshot filesystem upper layer (if using overlay fs)
  - Package everything into a snapshot directory
- `src/controller/restore.c` — restore coordinator:
  - Load controller state from snapshot
  - Restore CRIU images for each sandbox container
  - Re-establish unix socket connections between shim and controller
  - Re-map shared memory regions
  - Resume all processes
  - Optionally: modify PRNG seeds before resume (for multiverse branching)
- `scripts/snapshot.sh` — convenience script: `./snapshot.sh create <name>` / `./snapshot.sh restore <name>`
- SBP messages: PREPARE_SNAPSHOT (controller → shim: flush state), SNAPSHOT_READY (shim → controller: ready to freeze)

## Tests

- Simple process: checkpoint a `sleep` process under linbox → restore → process continues sleeping
- PostgreSQL: checkpoint after inserting 100 rows → restore → SELECT returns 100 rows
- Virtual time preserved: snapshot at virtual time T → restore → clock_gettime returns T
- PRNG state preserved: snapshot → restore → next getrandom returns same bytes as original continuation
- Multiverse: snapshot → restore with seed A → result X. Snapshot → restore with seed B → result Y ≠ X
- Unix socket reconnection: shim re-connects to controller after restore
- Shared memory re-mapped: shim reads correct virtual time after restore
- Filesystem: files created before snapshot exist after restore
- Snapshot size: minimal PostgreSQL (fresh initdb) → snapshot < 500MB

---

[← Back](../story.md)
