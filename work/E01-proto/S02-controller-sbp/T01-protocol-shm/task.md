# T01: SBP Protocol + Shared Memory

**Story:** [S02: Controller + SBP Protocol](../story.md)
**Status:** Backlog

## Description

Define the SBP (Sandbox Binary Protocol) wire format and shared memory layout. This is the communication foundation between liblinbox.so and the controller.

## Deliverables

- `src/common/sbp.h` — message types (HELLO, ACK, SET_TIME, SET_SEED, REGISTER_PROCESS), wire format (little-endian, length-prefixed), version field
- `src/common/sbp.c` — serialization/deserialization functions
- `src/common/shm-layout.h` — shared memory region structure:
  - Header: magic (0x4C494E42 "LINB"), version, global seqlock, heartbeat timestamp
  - Virtual time (struct timespec) protected by **seqlock** — writer increments sequence counter (odd = writing, even = stable), reader retries if sequence changed or is odd. This is needed because struct timespec is 16 bytes and cannot be read atomically on x86-64 without SSE/cmpxchg16b
  - PRNG seed
  - Flags (paused, stepping, etc.)
  - Per-process slots
  - **Extensible design:** layout must accommodate future policy sections (network policies, filesystem path mappings, DNS overrides) without breaking backward compatibility. Reserve space with explicit section offsets in header. See ld-preload.md research "Shared Memory Layout" for target ~6K layout.
- `src/common/shm.c` — mmap create/attach/detach helpers

## Tests

- Round-trip: serialize → deserialize each message type, verify fields match
- Boundary: max-size messages, zero-length payloads
- Shared memory: write time from one process, read from another via seqlock, verify consistency (no torn reads)
- Shared memory: concurrent reads from multiple threads while writer updates at high frequency (10K writes/sec) — no torn values
- Error: invalid message type → graceful error, not crash

---

[← Back](../story.md)
