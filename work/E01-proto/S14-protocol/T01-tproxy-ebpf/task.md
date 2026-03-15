# T01: TPROXY + eBPF Wire Protocol Interception

**Story:** [S14: Protocol-Level Interception](../story.md)
**Status:** Backlog

## Description

Set up transparent proxying for wire protocol interception. TPROXY redirects traffic to a local proxy within the network namespace. The proxy parses the wire protocol, logs queries/commands, and can inject faults or latency. eBPF TC programs provide an alternative path for packet-level inspection without proxying.

## Deliverables

- `src/proxy/pg-proxy.c` — transparent proxy for PostgreSQL wire protocol:
  - Accept TPROXY-redirected connections
  - Parse Postgres wire protocol: startup, query, parse/bind/execute, terminate
  - Forward to real PostgreSQL backend
  - Log queries, execution time
  - Hook points for controller: pre-query (can delay/reject), post-query (can modify result)
- `src/proxy/tproxy-setup.c` — TPROXY configuration:
  - iptables TPROXY rule to redirect traffic destined for port 5432 to local proxy
  - ip rule + ip route for policy-based routing (required by TPROXY)
  - Dynamic add/remove rules per protocol/port
- `src/ebpf/tc-inspect.c` (optional) — eBPF TC program:
  - Attach to container's veth interface
  - Inspect packet headers, match protocol signatures
  - Emit events to userspace via perf buffer (for logging without proxying)
- SBP messages: REGISTER_PROTOCOL_PROXY, PROTOCOL_EVENT

## Tests

- TPROXY rule installed → Postgres client connects to container, traffic redirected to proxy
- Proxy logs: "Query: SELECT 1" for every SELECT query
- Proxy forwards query to real backend → client gets correct result
- Controller injects 100ms delay on queries matching "SELECT.*FROM orders" → client sees 100ms extra latency
- Controller rejects query → client gets error response
- Non-Postgres traffic (port 6379 for Redis) → passes through unaffected
- Proxy crash → TPROXY rule still active, but traffic fails (document behavior, plan recovery)
- Throughput: pgbench through proxy → overhead < 100μs per query

---

[← Back](../story.md)
