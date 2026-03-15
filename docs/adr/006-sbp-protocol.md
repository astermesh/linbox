# ADR-006: SBP Protocol over Unix Socket

**Status:** Accepted
**Date:** 2025-05
**Context:** E01-S02 design

## Decision

Use a custom binary protocol (SBP — Sandbox Binary Protocol) over Unix domain sockets for controller-to-shim communication. Hot-path data (time, PRNG seed) is communicated via shared memory, not SBP.

## Context

The controller needs to send commands to the shim (SET_TIME, SET_SEED, etc.) and the shim needs to register with the controller (HELLO, REGISTER_PROCESS). This is a control plane — low frequency, not hot path.

## Rationale

- **Unix socket** — zero-copy on localhost, supports fd passing, works inside containers via volume mount
- **Binary protocol** — minimal parsing overhead, compact wire format
- **Length-prefixed, versioned messages** — forward-compatible, easy to extend
- **Shared memory for hot path** — SBP is only for control messages; time/random reads go through shared memory (ADR-002)

**Message types (proto phase):**
- `HELLO` — shim announces itself to controller
- `ACK` — controller acknowledges
- `SET_TIME` — controller sets virtual time (also updates shared memory)
- `SET_SEED` — controller sets PRNG seed
- `REGISTER_PROCESS` — shim registers a new process (after fork)

## Alternatives Considered

- **gRPC/protobuf** — too heavy for a C shim loaded into every process. Adds large dependency.
- **JSON over socket** — parsing overhead, no binary efficiency.
- **Shared memory only (no socket)** — cannot do bidirectional communication, cannot handle registration/handshake.
- **Named pipes (FIFO)** — unidirectional, would need two per connection. Unix sockets are simpler.

## Consequences

- SBP protocol spec must be defined precisely (wire format, endianness, message types)
- Socket path passed to shim via `LINBOX_SOCK` environment variable
- Controller must handle multiple concurrent shim connections (one per process)
- Protocol is internal — no stability guarantees across major versions

---

[← Back to ADRs](README.md)
