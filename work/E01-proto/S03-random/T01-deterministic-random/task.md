# T01: Deterministic Random

**Story:** [S03: Random Interception](../story.md)
**Status:** Backlog

## Description

Full deterministic random interception. Seeded PRNG replaces all sources of randomness accessible through libc.

## Deliverables

- `src/shim/prng.h` / `src/shim/prng.c` — ChaCha20 PRNG implementation (or embedded tiny library), init from seed, generate bytes
- `src/shim/random.c` — interception of:
  - `getrandom(buf, len, flags)` — fill buf from PRNG, handle GRND_RANDOM and GRND_NONBLOCK flags
  - `open()` — detect opens of `/dev/urandom`, `/dev/random`, return a virtual fd
  - `read()` — if fd is virtual, fill from PRNG instead of real read
  - `close()` — clean up virtual fd tracking
- Seed from controller via shared memory (field in shm-layout)
- Per-process PRNG state (prepared for S05 fork handling)

## Tests

- Two runs, same seed → `getrandom()` returns identical bytes
- Two runs, different seed → different bytes
- `getrandom()` with GRND_RANDOM flag → deterministic
- `getrandom()` with GRND_NONBLOCK → returns immediately, deterministic
- `open("/dev/urandom")` + `read()` → deterministic bytes
- `open("/dev/random")` + `read()` → deterministic bytes
- Multiple `read()` calls on same fd → sequential PRNG output, deterministic
- `close()` virtual fd → subsequent open gets fresh state? or continues? (define behavior)
- Stress: 10,000 `getrandom()` calls → all deterministic, no crashes, no memory leaks
- Real fd (e.g. regular file) not affected by interception

---

[← Back](../story.md)
