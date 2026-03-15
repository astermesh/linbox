# T01: eBPF Syscall Monitoring

**Story:** [S16: eBPF Observability](../story.md)
**Status:** Backlog

## Description

Implement eBPF-based syscall monitoring for sandbox processes. Attach tracepoint or kprobe programs to syscall entry/exit points, collect counters and latency data, stream events to userspace.

## Deliverables

- `src/ebpf/syscall-monitor.bpf.c` — eBPF program:
  - Attach to `tracepoint/raw_syscalls/sys_enter` and `sys_exit`
  - Filter by PID namespace or cgroup (only sandbox processes)
  - Per-syscall counter map (BPF_MAP_TYPE_HASH: syscall_nr → count)
  - Latency histogram map (BPF_MAP_TYPE_HASH: syscall_nr → histogram buckets)
  - Record entry timestamp on sys_enter, compute latency on sys_exit
  - Optional: emit specific events to perf buffer (e.g., every connect(), every openat())
- `src/ebpf/syscall-monitor.c` — userspace loader and reader:
  - Load eBPF program via libbpf
  - Read counter and histogram maps periodically
  - Format and display metrics (top-style view or JSON output)
  - Support start/stop monitoring dynamically
- `src/controller/observability.c` — controller integration:
  - SBP message: GET_METRICS → return current syscall counters and latencies
  - Optional: expose metrics via simple HTTP endpoint or unix socket
- Build integration: separate build target `make monitor` (eBPF requires kernel headers)

## Tests

- Load eBPF program → no errors, attaches to tracepoints
- Run sandboxed process making clock_gettime calls → counter increments
- Latency histogram shows expected distribution (~100ns for LD_PRELOAD, ~800ns for seccomp)
- Filter by PID namespace → only sandbox processes counted
- Non-sandbox processes → not counted
- `make monitor` → builds eBPF program and userspace loader
- Enable/disable monitoring → overhead drops to 0 when disabled
- Concurrent monitoring: multiple sandbox processes → per-process breakdown available

---

[← Back](../story.md)
