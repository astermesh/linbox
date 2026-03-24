# S03: Random Interception

**Epic:** Proto
**Status:** Done

## Required Reading

- [LD_PRELOAD interception — random functions](../../rnd/boxing/ld-preload.md)
- [LinBox architecture — randomness section](../../rnd/boxing/linux-sandbox.md)
- [ADR-003: ChaCha20 PRNG](../../docs/adr/003-chacha20-prng.md)

## Business Result

A process produces deterministic random output given the same seed. Two runs with identical seed yield byte-for-byte identical random data.

## Scope

- Seeded PRNG (ChaCha20)
- `getrandom()`, `getentropy()` interception
- `arc4random()`, `arc4random_buf()`, `arc4random_uniform()` interception
- `rand()`, `srand()`, `random()`, `srandom()`, `rand_r()` — deterministic libc PRNG
- `/dev/urandom` and `/dev/random` interception via `open()` + `read()`
- `getauxval(AT_RANDOM)` overwrite for deterministic stack canaries
- RDRAND/RDSEED CPUID masking (disable hardware random instructions)
- Seed management via controller (SBP/shared memory)

## Tasks

- [T01: Deterministic random](T01-deterministic-random/task.md)

## Dependencies

- S01 (shim scaffold)
- S02 (controller for seed management)

## Acceptance Criteria

- Two runs with same seed produce identical getrandom() output
- `/dev/urandom` reads are deterministic
- Different seeds produce different output

---

[← Back](../epic.md)
