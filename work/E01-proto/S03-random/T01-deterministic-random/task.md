# T01: Deterministic Random

**Story:** [S03: Random Interception](../story.md)
**Status:** Done

## Description

Full deterministic random interception. Seeded PRNG replaces all sources of randomness accessible through libc.

## Deliverables

- `src/shim/prng.h` / `src/shim/prng.c` — ChaCha20 PRNG implementation (or embedded tiny library), init from seed, generate bytes
- `src/shim/random.c` — interception of:
  - `getrandom(buf, len, flags)` — fill buf from PRNG, handle GRND_RANDOM and GRND_NONBLOCK flags
  - `open()` — detect opens of `/dev/urandom`, `/dev/random`, return a virtual fd
  - `openat()` — same detection for `openat(fd, "/dev/urandom", ...)` (glibc routes `fopen` and many `open` calls through `openat` internally)
  - `read()` — if fd is virtual, fill from PRNG instead of real read
  - `close()` — clean up virtual fd tracking
  - `getentropy(buf, len)` — fill from PRNG (glibc 2.25+, wrapper around getrandom)
  - `arc4random()`, `arc4random_buf()`, `arc4random_uniform()` — fill from PRNG (glibc 2.36+; internally use getrandom, but explicit interception ensures coverage for static linking or non-glibc)
  - `rand()`, `srand()`, `random()`, `srandom()`, `rand_r()` — replace libc PRNG with deterministic implementation seeded from controller. Without this, `srand(time(NULL))` patterns produce non-deterministic sequences even with controlled time (because real srand has internal state we don't control)
- Overwrite `getauxval(AT_RANDOM)` 16 bytes in shim constructor with seeded value (deterministic stack canaries, Go runtime seed)
- RDRAND/RDSEED CPUID masking — disable hardware random instructions via CPUID interception so programs fall back to getrandom (Shadow approach). If CPUID masking is not available, intercept at seccomp level (S04)
- Seed from controller via shared memory (field in shm-layout)
- Per-process PRNG state (prepared for S05 fork handling)

## Verification

Note: current implementation covers deterministic libc/kernel random sources exercised by the test suite, but does not yet implement `getauxval(AT_RANDOM)` overwrite or CPUID masking for `RDRAND`/`RDSEED`. Those remaining deliverables must be completed before this task can be marked Done.


- Unit tests: PRNG logic, seed handling, deterministic byte generation
- Preload tests: `getrandom`, `getentropy`, `arc4random*`, `rand/random`, `/dev/urandom` and `/dev/random` interception in standalone binaries under `LD_PRELOAD`
- Pseudo-box tests: repeatability scenarios with same seed and changed seed across repeated controller-managed runs
- E2E tests: PostgreSQL deterministic UUID generation in containerized service (later, when S06 is active)
- Manual checks: compare outputs from repeated runs with same seed and different seeds

## Tests

- Two runs, same seed → `getrandom()` returns identical bytes
- Two runs, different seed → different bytes
- `getrandom()` with GRND_RANDOM flag → deterministic
- `getrandom()` with GRND_NONBLOCK → returns immediately, deterministic
- `open("/dev/urandom")` + `read()` → deterministic bytes
- `open("/dev/random")` + `read()` → deterministic bytes
- `openat(AT_FDCWD, "/dev/urandom", O_RDONLY)` + `read()` → deterministic bytes
- `fopen("/dev/urandom", "r")` + `fread()` → deterministic bytes (verifies openat path)
- Multiple `read()` calls on same fd → sequential PRNG output, deterministic
- `close()` virtual fd → subsequent open gets fresh state? or continues? (define behavior)
- Stress: 10,000 `getrandom()` calls → all deterministic, no crashes, no memory leaks
- `getentropy()` → deterministic bytes
- `arc4random()` → deterministic value, same across runs with same seed
- `arc4random_buf()` → deterministic bytes
- `arc4random_uniform(100)` → deterministic value in range
- `rand()` after `srand(X)` → deterministic sequence, same across runs
- `random()` after `srandom(X)` → deterministic sequence
- RDRAND/RDSEED: program that checks CPUID sees these as unavailable, falls back to getrandom
- Real fd (e.g. regular file) not affected by interception

---

[← Back](../story.md)
