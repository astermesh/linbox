# T02: Time Interception

**Story:** [S01: Time Interception](../story.md)
**Status:** Backlog

## Description

Implement LD_PRELOAD-based interception of all time-related libc functions. Return hardcoded fake time (2025-01-01 00:00:00 UTC). No controller — this proves the interception mechanism works.

## Deliverables

- `src/shim/resolve.h` — macros for lazy `dlsym(RTLD_NEXT, ...)` resolution with bootstrap guard
- `src/shim/linbox.h` — internal shared header (global state struct, config)
- `src/shim/linbox.c` — `__attribute__((constructor))` initialization, stderr log "linbox: shim loaded (pid=...)"
- `src/shim/time.c` — interception of:
  - `clock_gettime()` — virtual time for ALL clock IDs: CLOCK_REALTIME, CLOCK_REALTIME_COARSE, CLOCK_MONOTONIC, CLOCK_MONOTONIC_COARSE, CLOCK_MONOTONIC_RAW, CLOCK_BOOTTIME, CLOCK_PROCESS_CPUTIME_ID, CLOCK_THREAD_CPUTIME_ID
  - CPUTIME model: derived from virtual wall time (process appears 100% CPU-bound). This matches single-core simulation semantics (Antithesis, Shadow, FDB Sim2) where CPU time ≈ wall time. Passing real CPU time would break the illusion — the process could detect simulation by comparing wall vs CPU time.
  - `gettimeofday()`
  - `time()`
  - `clock_getres()` — return consistent resolution for virtual clocks
  - `clock()` — CPU time, derived from virtual wall time (same model as CPUTIME clock IDs)
  - `times()` — process times, derived from virtual wall time
  - `timespec_get()` — C11 time function, delegates to virtual clock_gettime

## Tests

- Unit test per function: `clock_gettime(CLOCK_REALTIME)` returns fake time
- Unit test per clock ID: REALTIME, REALTIME_COARSE, MONOTONIC, MONOTONIC_COARSE, MONOTONIC_RAW, BOOTTIME, PROCESS_CPUTIME_ID, THREAD_CPUTIME_ID — all return controlled values
- CLOCK_PROCESS_CPUTIME_ID returns virtual time derived from wall time (not real CPU time)
- `gettimeofday()` returns fake time with microsecond precision
- `time()` returns fake time as `time_t`
- Integration: `LD_PRELOAD=liblinbox.so date` prints "Wed Jan  1 00:00:00 UTC 2025"
- Negative: without LD_PRELOAD, `date` shows real time (sanity check)
- `clock_getres()` returns consistent resolution for all clock IDs
- `clock()` returns virtual CPU time
- `times()` returns virtual process times
- `timespec_get()` returns virtual time (TIME_UTC base)
- `dlsym(RTLD_NEXT)` correctly resolves real libc function (can call through to real clock_gettime when needed)

---

[← Back](../story.md)
