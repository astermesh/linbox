# ADR-003: ChaCha20 PRNG for Deterministic Randomness

**Status:** Accepted
**Date:** 2025-05
**Context:** R01-boxing research, E01-S03 design

## Decision

Use ChaCha20-based PRNG seeded from the controller for all randomness interception. Each process gets a unique stream derived from the parent seed and process ID.

## Context

LinBox must make all randomness deterministic: `getrandom()`, `/dev/urandom`, `/dev/random`, and `getauxval(AT_RANDOM)` (used for stack canaries and Go runtime seeding).

## Rationale

- **ChaCha20** is cryptographically strong, fast in software, and widely used as a CSPRNG (Linux kernel uses it for `/dev/urandom`)
- **Deterministic with seed:** same seed → same byte stream, guaranteed
- **Per-process derivation:** `child_seed = ChaCha20(parent_seed, child_pid)` ensures forked processes get unique but reproducible streams
- **AT_RANDOM overwrite:** 16 bytes in the ELF auxiliary vector, overwritten in shim constructor to ensure stack canaries and Go runtime use deterministic values

## Alternatives Considered

- **xorshift/PCG** — fast but not cryptographically strong. Services expecting `/dev/urandom` quality might behave differently.
- **AES-CTR** — requires hardware AES-NI for good performance. ChaCha20 is faster in software.
- **Pass-through with recorded values** — record real random and replay. Complex, non-deterministic across runs without recording infrastructure.

## Consequences

- Seed must be communicated from controller to shim (via shared memory or SBP)
- Fork handling must derive new seed before child uses any randomness
- `open("/dev/urandom")` and `open("/dev/random")` must be intercepted to return virtual file descriptors
- `read()` on those virtual fds must fill from PRNG instead of kernel

---

[← Back to ADRs](README.md)
