# ADR-010: Network Simulation via tc/netem

**Status:** Accepted
**Date:** 2025-05
**Context:** R01-boxing research

## Decision

Use Linux Traffic Control (tc) with Network Emulation (netem) for network simulation: latency, packet loss, jitter, bandwidth, and reordering. Use iptables/nftables for network partitioning.

## Context

LinBox needs to simulate realistic network conditions between boxes — variable latency, packet loss, partitions. This must work with real TCP/UDP stacks (not simulated).

## Rationale

- **Kernel-level, near-zero overhead:** tc/netem operates in the kernel's network stack, no userspace overhead per packet
- **Rich feature set:** configurable latency distributions, correlated loss, bandwidth limiting, packet reordering, duplication
- **Per-interface granularity:** each veth pair can have independent network characteristics
- **iptables for partitions:** simple `DROP` rules simulate network partitions cleanly
- **No code changes:** works transparently with any network-using application

## Alternatives Considered

- **Userspace proxy per connection** — high overhead, complex, breaks some protocols
- **eBPF TC** — more programmable but harder to configure. Reserved for Phase 3+ protocol-level observation.
- **Simulated TCP stack (Shadow approach)** — full control but reimplements TCP. Too complex for proto.

## Consequences

- Network namespace + veth pair per box (provided by container runtime)
- Controller manages tc/netem rules via `tc` commands
- Network partitions via iptables rule insertion/removal
- Real TCP/UDP behavior preserved (retransmits, congestion control work normally)

---

[← Back to ADRs](README.md)
