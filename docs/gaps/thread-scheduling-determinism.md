# Gap: Thread Scheduling Determinism

**Severity:** High (fundamental limitation)
**Blocks:** Full determinism for multi-threaded services

## What's Missing

LinBox intercepts syscalls and controls time/randomness, but does not control thread scheduling. The OS scheduler decides which thread runs when. This means:

- Two runs with the same seed may produce different thread interleavings
- Race conditions in the target service will manifest differently across runs
- Byte-for-byte reproducibility is only guaranteed for single-threaded or fork-based (not thread-based) workloads

## What's Known

Systems that solve this:

- **Antithesis:** Single vCPU per VM — only one thread runs at a time, deterministic scheduling
- **rr / Hermit:** Use hardware performance counters (PMU) to count retired conditional branches and enforce deterministic scheduling
- **FDB Sim2 / MadSim / TigerBeetle:** Single-threaded by design — no scheduling non-determinism

LinBox's target (PostgreSQL) uses fork-per-backend, not threads, so this is less critical for the proto phase. But multi-threaded services (Node.js worker threads, Go goroutines) will have non-deterministic scheduling.

## Resolution Path

Options for future phases:

1. **Accept the limitation** — document that LinBox provides "almost deterministic" simulation for multi-threaded workloads (time, randomness, network are deterministic; thread scheduling is not)
2. **Single-core pinning** — use `taskset` / cgroups to pin each box to one CPU core. Reduces scheduling non-determinism but doesn't eliminate it.
3. **Deterministic scheduler** — extremely difficult. Would require kernel module or ptrace-based thread control (like Hermit). Out of scope.

---

[← Back to gaps](README.md)
