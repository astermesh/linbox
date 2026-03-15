# S16: eBPF Observability

**Epic:** Proto
**Status:** Backlog

## Business Result

Syscall activity inside the sandbox is observable in real-time. Performance metrics (interception overhead, syscall frequency, latency distribution) are available for debugging and optimization. This is a development and operational tool, not a simulation feature.

## Scope

- eBPF tracepoint/kprobe programs for syscall monitoring
- Per-syscall counters (total, intercepted, passed-through)
- Latency histograms for intercepted syscalls
- Perf buffer or ring buffer for event streaming to userspace
- CLI tool or controller endpoint for viewing metrics

## Tasks

- [T01: eBPF syscall monitoring](T01-ebpf-monitoring/task.md)

## Dependencies

- S06 (Docker — container for eBPF program loading)
- S04 (seccomp — understanding which syscalls are intercepted)

## Acceptance Criteria

- Real-time view of syscall counts per type (clock_gettime, getrandom, connect, etc.)
- Latency histogram for intercepted syscalls (p50, p99, p99.9)
- Can filter by PID (see only sandbox processes)
- Overhead of monitoring < 1% when enabled
- Monitoring can be enabled/disabled dynamically

---

[← Back](../epic.md)
