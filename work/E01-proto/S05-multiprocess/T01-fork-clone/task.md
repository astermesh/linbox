# T01: Fork/Clone Interception

**Story:** [S05: Multi-Process Support](../story.md)
**Status:** Backlog

## Description

Intercept process creation syscalls. On fork, reinitialize per-process state: new PRNG stream (derived from parent seed + child PID), re-register with controller. On execve, verify LD_PRELOAD environment variable is preserved.

## Deliverables

- `src/shim/process.c` — interception of:
  - `fork()` — wrap real fork, reinit state in child
  - `clone()` — detect new-process vs new-thread (check CLONE_VM flag), reinit for new process
  - `clone3()` — same as clone but with `struct clone_args`
  - `execve()` — verify `LD_PRELOAD` in environment, log if missing
- Update `src/shim/linbox.c` — `reinit_state()` function: new PRNG stream, register with controller via SBP REGISTER_PROCESS message
- Update `src/shim/prng.c` — derive child seed from parent seed + pid

## Tests

- `fork()` → parent and child get different `getrandom()` output
- `fork()` → parent and child see identical `clock_gettime()` value
- Deep fork: grandchild (3 levels) under control, unique PRNG stream
- `clone()` with CLONE_VM (thread) → shares PRNG state (or thread-local?)
- `clone()` without CLONE_VM (process) → new PRNG stream
- `execve("/bin/sh", ...)` → child process has shim loaded (check via clock_gettime)
- Controller sees REGISTER_PROCESS for each fork
- fork() + seccomp → child inherits seccomp filter, direct syscalls still intercepted
- Stress: 100 rapid forks → all children registered, no resource leaks, controller handles all
- Orphan cleanup: child exits → controller removes registration

---

[← Back](../story.md)
