# T01: seccomp-bpf + SIGSYS Handler

**Story:** [S04: Seccomp Safety Net](../story.md)
**Status:** Done

## Description

Install a seccomp-bpf filter that traps targeted syscalls (SECCOMP_RET_TRAP) and allows everything else. The SIGSYS handler extracts syscall number and arguments from ucontext and dispatches to existing interception handlers.

## Deliverables

- `src/shim/seccomp.c` — BPF filter construction and installation via `prctl(PR_SET_SECCOMP)` or `seccomp(SECCOMP_SET_MODE_FILTER)`
  - SECCOMP_RET_TRAP for: `clock_gettime`, `gettimeofday`, `time`, `getrandom`
  - SECCOMP_RET_ERRNO(ENOSYS) for: `io_uring_setup`, `io_uring_enter`, `io_uring_register` — block io_uring to prevent bypass of syscall interception (io_uring submits operations via shared memory ring, invisible to both LD_PRELOAD and per-syscall seccomp)
  - SECCOMP_RET_ALLOW for everything else
  - Filter installed in shim constructor after LD_PRELOAD handlers are set up
- `src/shim/syscall-wrap.c` — LD_PRELOAD interception of the `syscall()` libc wrapper function. Programs calling `syscall(SYS_clock_gettime, ...)` bypass specific LD_PRELOAD hooks but still go through the libc `syscall()` wrapper. Intercept this wrapper, dispatch by syscall number to existing handlers. This is complementary to seccomp: seccomp catches inline assembly `SYSCALL` instructions, this catches the libc `syscall()` function.
- `src/shim/sigsys.c` — SIGSYS handler:
  - Extract syscall number from `si_syscall`
  - Extract arguments from `ucontext_t->uc_mcontext.gregs`
  - Dispatch to `handle_clock_gettime()`, `handle_getrandom()`, etc.
  - Set return value via `REG_RAX`
  - For unknown syscalls: execute via real syscall (should not happen with correct filter)

## Tests

- Program makes direct `syscall(__NR_clock_gettime, CLOCK_REALTIME, &ts)` → gets fake time
- Program makes direct `syscall(__NR_gettimeofday, &tv, NULL)` → gets fake time
- Program makes direct `syscall(__NR_time, &t)` → gets fake time
- Program makes direct `syscall(__NR_getrandom, buf, len, 0)` → deterministic output
- Direct syscall to `write()` → passes through normally (not trapped)
- Direct syscall to `read()` → passes through normally
- SIGSYS handler does not interfere with normal SIGUSR1/SIGUSR2 handling
- Seccomp filter survives `fork()` (child inherits filter)
- Stress: 10K direct syscalls in tight loop → no crashes, correct values
- `io_uring_setup()` returns ENOSYS (application falls back to regular I/O)
- `syscall(SYS_clock_gettime, CLOCK_REALTIME, &ts)` via libc wrapper → returns fake time (caught by syscall() interception, not seccomp)
- `syscall(SYS_getrandom, buf, len, 0)` via libc wrapper → deterministic output
- Combined: LD_PRELOAD + syscall() wrapper + seccomp all active → LD_PRELOAD handles specific libc calls, syscall() wrapper handles generic libc syscall(), seccomp handles inline assembly, no double interception

---

[← Back](../story.md)
