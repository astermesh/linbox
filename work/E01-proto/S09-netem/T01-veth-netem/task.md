# T01: veth + tc/netem + iptables + cgroups

**Story:** [S09: Network Simulation](../story.md)
**Status:** Backlog

## Description

Set up kernel-level network simulation infrastructure. Each sandbox container gets a veth pair connecting it to a LinBox bridge. tc/netem on the veth interface provides configurable latency, loss, jitter, and bandwidth. iptables rules enable network partition simulation. cgroups v2 provides resource control.

## Deliverables

- `src/controller/network-sim.c` — network simulation manager:
  - Create/destroy veth pairs for sandbox containers
  - Apply tc/netem qdisc rules (delay, loss, jitter, rate, reorder, duplicate, corrupt)
  - Per-destination rules via tc filter + classful qdisc (HTB + netem per class)
  - Apply/remove iptables rules for partition simulation (DROP, REJECT)
  - Support dynamic changes (update qdisc without tearing down)
- `src/controller/cgroup-manager.c` — cgroup v2 resource control:
  - CPU quota (cpu.max) — simulate CPU contention
  - Memory limit (memory.max) — constrain service
  - I/O bandwidth (io.max) — simulate slow disk
- SBP messages: SET_NETWORK_CONDITION, SET_PARTITION, SET_RESOURCE_LIMIT
- `scripts/setup-bridge.sh` — create LinBox bridge network (br-linbox)
- Update `docker-compose.yml` — containers use LinBox bridge network, NET_ADMIN capability for tc/iptables

## Tests

- veth pair created → container can ping LinBox bridge
- `tc qdisc add netem delay 50ms` → measured RTT ~50ms (±5ms)
- `tc qdisc change netem delay 100ms 20ms distribution normal` → latency 100ms ±20ms with normal distribution
- `tc qdisc add netem loss 10%` → ~10% packet loss over 1000 pings
- `tc qdisc add netem rate 1mbit` → throughput limited to ~1 Mbps
- iptables DROP rule → no connectivity to target
- Remove DROP rule → connectivity restored
- Per-destination rules: Box A → Box B has 50ms latency, Box A → Box C has 10ms
- cgroup CPU 50% → process gets ~50% CPU time under load
- cgroup memory 256MB → OOM if process exceeds limit
- Dynamic change: update netem params → new conditions take effect immediately (within 1 packet)
- Controller SET_NETWORK_CONDITION message → netem updated

---

[← Back](../story.md)
