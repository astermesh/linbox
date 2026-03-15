# S14: Protocol-Level Interception

**Epic:** Proto
**Status:** Backlog

## Required Reading

- [LinBox architecture — Layer 4](../../rnd/boxing/linux-sandbox.md)
- [Sandbox alternatives — network interception, eBPF](../../rnd/boxing/sandbox-alternatives.md)

## Business Result

Wire protocol traffic (Postgres wire protocol, Redis RESP, HTTP) is observable and interceptable at the application layer. This is Layer 4 of the architecture — IBI (Inbound Box Interface) coverage for LinBox containers. Enables request-level logging, latency injection, and fault injection at the protocol level.

## Scope

- TPROXY setup for transparent proxying within network namespace
- eBPF TC programs for programmable packet-level interception
- Wire protocol parsers for at least one protocol (Postgres wire)
- Integration with controller for protocol-level policy decisions

## Tasks

- [T01: TPROXY + eBPF wire protocol interception](T01-tproxy-ebpf/task.md)

## Dependencies

- S06 (Docker — container networking)
- S08 (network interception — socket-level foundation)
- S09 (tc/netem — network infrastructure)

## Acceptance Criteria

- Postgres wire protocol traffic between client and PostgreSQL container is observable
- Controller can see individual SQL queries passing through
- Controller can inject latency on specific query patterns
- Non-Postgres traffic passes through unaffected
- Overhead < 100μs per proxied request

---

[← Back](../epic.md)
