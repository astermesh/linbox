# S09: Network Simulation (tc/netem)

**Epic:** Proto
**Status:** Backlog

## Business Result

Network conditions between boxes are controllable. Latency, jitter, packet loss, bandwidth limits, and network partitions can be configured dynamically from the controller. This is Layer 2 of the LinBox architecture — kernel-level network simulation with near-zero overhead.

## Scope

- veth pair setup between sandbox container and LinBox bridge
- tc/netem configuration for latency, loss, jitter, bandwidth, reordering
- iptables/nftables rules for network partition simulation
- cgroups v2 integration for CPU quota, memory limit, I/O bandwidth
- Controller API for dynamic network condition changes
- Per-destination rules (different latency to different boxes)

## Tasks

- [T01: veth + tc/netem + iptables + cgroups](T01-veth-netem/task.md)

## Dependencies

- S06 (Docker setup — container networking infrastructure)
- S08 (network interception — coordination)

## Acceptance Criteria

- Controller sets 50ms latency between Box A and Box B → ping shows ~50ms RTT
- Controller sets 10% packet loss → observed loss rate matches
- Controller drops all traffic to Box B → network partition, no connectivity
- Controller restores connectivity → traffic flows again
- cgroups limit CPU to 50% → Box sees reduced CPU availability
- All changes are dynamic — no container restart needed

---

[← Back](../epic.md)
