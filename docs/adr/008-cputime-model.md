# ADR-008: CPUTIME Derived from Virtual Wall Time

**Status:** Accepted
**Date:** 2025-05
**Context:** E01-S01 design

## Decision

Report CPU time (CLOCK_PROCESS_CPUTIME_ID, CLOCK_THREAD_CPUTIME_ID) as equal to virtual wall time. The process appears to be 100% CPU-bound.

## Context

Some programs compare wall time vs CPU time to detect virtualization or measure I/O wait. If wall time is virtual but CPU time is real, the discrepancy reveals the simulation.

## Rationale

- **Single-core simulation semantics:** LinBox simulates each box as a single-core machine (like Antithesis, Shadow, FDB Sim2). In a single-core model with no idle time, CPU time equals wall time.
- **Consistency:** Prevents detection via `clock_gettime(CLOCK_PROCESS_CPUTIME_ID)` vs `clock_gettime(CLOCK_REALTIME)` comparison.
- **Simplicity:** No need to track actual CPU usage — just return the same virtual time.

## Alternatives Considered

- **Pass through real CPU time** — reveals simulation. Programs using CPU time for profiling or scheduling get inconsistent data.
- **Scale CPU time proportionally** — complex, requires tracking actual CPU cycles consumed. Over-engineering for proto phase.

## Consequences

- All `CLOCK_*CPUTIME*` variants return virtual wall time
- Profiling tools inside the sandbox will show misleading CPU utilization (always 100%)
- Acceptable trade-off for determinism

---

[← Back to ADRs](README.md)
