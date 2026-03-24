# T01: Fork/Clone Interception

**Story:** [S05: Multi-Process Support](../story.md)
**Status:** Done

## Description

Intercept process creation syscalls. On fork, reinitialize per-process state: new PRNG stream (derived from parent seed + child PID), re-register with controller. On execve, verify LD_PRELOAD environment variable is preserved.

## Deliverables

- `src/shim/process.c` — interception of:
  - `fork()` — wrap real fork, reinit state in child
  - `vfork()` — intercept and delegate to fork wrapper (same as Shadow — vfork shares parent memory, unsafe for state reinit, so treat as fork)
  - `clone()` — detect new-process vs new-thread (check CLONE_VM flag), reinit for new process
  - `clone3()` — same as clone but with `struct clone_args`
  - `execve()` — ensure `LD_PRELOAD` is in the target environment: if missing from envp, inject it before calling real execve (LD_PRELOAD is inherited automatically when envp passes through, but explicit envp without it would lose the shim)
  - `execvp()`, `execvpe()`, `execl()`, `execle()`, `execlp()` — exec family variants; ensure all go through the same LD_PRELOAD injection logic
  - `posix_spawn()`, `posix_spawnp()` — some programs use instead of fork+exec; ensure LD_PRELOAD is in the spawn attributes environment
- Update `src/shim/linbox.c` — `reinit_state()` function: new PRNG stream, register with controller via SBP REGISTER_PROCESS message
- Update `src/shim/prng.c` — derive child seed from parent seed + pid

## Tests

- `fork()` → parent and child get different `getrandom()` output
- `fork()` → parent and child see identical `clock_gettime()` value
- Deep fork: grandchild (3 levels) under control, unique PRNG stream
- `clone()` with CLONE_VM (thread) → shares PRNG state (or thread-local?)
- `clone()` without CLONE_VM (process) → new PRNG stream
- `execve("/bin/sh", ...)` → child process has shim loaded (check via clock_gettime)
- `vfork()` → behaves as fork, child gets new PRNG stream
- `execve()` with explicit envp without LD_PRELOAD → shim injects it, child still has shim
- `execvp("/bin/sh")` → child has shim loaded
- `posix_spawn()` → child has shim loaded, clock_gettime returns fake time
- Controller sees REGISTER_PROCESS for each fork
- fork() + seccomp → child inherits seccomp filter, direct syscalls still intercepted
- Stress: 100 rapid forks → all children registered, no resource leaks, controller handles all
- Orphan cleanup: child exits → controller removes registration

---

[← Back](../story.md)
