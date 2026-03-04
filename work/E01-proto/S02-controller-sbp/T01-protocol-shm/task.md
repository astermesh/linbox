# T01: SBP Protocol + Shared Memory

**Story:** [S02: Controller + SBP Protocol](../story.md)
**Status:** Backlog

## Description

Define the SBP (Sandbox Binary Protocol) wire format and shared memory layout. This is the communication foundation between liblinbox.so and the controller.

## Deliverables

- `src/common/sbp.h` — message types (HELLO, ACK, SET_TIME, SET_SEED, REGISTER_PROCESS), wire format (little-endian, length-prefixed), version field
- `src/common/sbp.c` — serialization/deserialization functions
- `src/common/shm-layout.h` — shared memory region structure:
  - Virtual time (struct timespec, atomically readable)
  - PRNG seed
  - Flags (paused, stepping, etc.)
  - Per-process slots
- `src/common/shm.c` — mmap create/attach/detach helpers

## Tests

- Round-trip: serialize → deserialize each message type, verify fields match
- Boundary: max-size messages, zero-length payloads
- Shared memory: write time atomically from one process, read from another, verify consistency
- Shared memory: concurrent reads from multiple threads while writer updates
- Error: invalid message type → graceful error, not crash

---

[← Back](../story.md)
