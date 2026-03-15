# Gap: SBP Protocol Specification

**Severity:** Medium
**Blocks:** E01-S02 (must be resolved before implementation)
**Related ADR:** [ADR-006](../adr/006-sbp-protocol.md)

## What's Missing

The SBP (Sandbox Binary Protocol) is mentioned in the plans but the wire format is not defined:

- Exact byte layout of messages (header structure, field sizes, alignment)
- Endianness handling (little-endian decided, but encoding details missing)
- Version negotiation during HELLO handshake
- Error handling and recovery (what happens on malformed messages?)
- Message flow diagrams (who sends what, when)

## What's Decided

- Binary protocol, little-endian, length-prefixed, versioned
- Unix domain socket transport
- Message types: HELLO, ACK, SET_TIME, SET_SEED, REGISTER_PROCESS
- Hot-path data goes via shared memory, not SBP

## Resolution Path

Define the protocol spec as part of E01-S02-T01 implementation. Can start with a minimal spec and extend.

---

[← Back to gaps](README.md)
