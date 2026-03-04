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
  - `clock_gettime()` — all clock IDs (CLOCK_REALTIME, CLOCK_MONOTONIC, CLOCK_BOOTTIME, CLOCK_MONOTONIC_RAW, CLOCK_PROCESS_CPUTIME_ID, CLOCK_THREAD_CPUTIME_ID)
  - `gettimeofday()`
  - `time()`

## Tests

- Unit test per function: `clock_gettime(CLOCK_REALTIME)` returns fake time
- Unit test per clock ID: REALTIME, MONOTONIC, BOOTTIME all return controlled values
- `gettimeofday()` returns fake time with microsecond precision
- `time()` returns fake time as `time_t`
- Integration: `LD_PRELOAD=liblinbox.so date` prints "Wed Jan  1 00:00:00 UTC 2025"
- Negative: without LD_PRELOAD, `date` shows real time (sanity check)
- `dlsym(RTLD_NEXT)` correctly resolves real libc function (can call through to real clock_gettime when needed)

---

[← Back](../story.md)
