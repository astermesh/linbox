# T01: seccomp-bpf + SIGSYS Handler

**Story:** [S04: Seccomp Safety Net](../story.md)
**Status:** Backlog

## Description

Install a seccomp-bpf filter that traps targeted syscalls (SECCOMP_RET_TRAP) and allows everything else. The SIGSYS handler extracts syscall number and arguments from ucontext and dispatches to existing interception handlers.

## Deliverables

- `src/shim/seccomp.c` — BPF filter construction and installation via `prctl(PR_SET_SECCOMP)` or `seccomp(SECCOMP_SET_MODE_FILTER)`
  - SECCOMP_RET_TRAP for: `clock_gettime`, `gettimeofday`, `time`, `getrandom`
  - SECCOMP_RET_ALLOW for everything else
  - Filter installed in shim constructor after LD_PRELOAD handlers are set up
- `src/shim/sigsys.c` — SIGSYS handler:
  - Extract syscall number from `si_syscall`
  - Extract arguments from `ucontext_t->uc_mcontext.gregs`
  - Dispatch to `handle_clock_gettime()`, `handle_getrandom()`, etc.
  - Set return value via `REG_RAX`
  - For unknown syscalls: execute via real syscall (should not happen with correct filter)

## Tests

- Program makes direct `syscall(__NR_clock_gettime, CLOCK_REALTIME, &ts)` → gets fake time
- Program makes direct `syscall(__NR_gettimeofday, &tv, NULL)` → gets fake time
- Program makes direct `syscall(__NR_getrandom, buf, len, 0)` → deterministic output
- Direct syscall to `write()` → passes through normally (not trapped)
- Direct syscall to `read()` → passes through normally
- SIGSYS handler does not interfere with normal SIGUSR1/SIGUSR2 handling
- Seccomp filter survives `fork()` (child inherits filter)
- Stress: 10K direct syscalls in tight loop → no crashes, correct values
- Combined: LD_PRELOAD + seccomp both active → LD_PRELOAD handles libc calls, seccomp handles direct calls, no double interception

---

[← Back](../story.md)
