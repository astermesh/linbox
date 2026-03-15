# T01: Signal Interception

**Story:** [S11: Signal Management](../story.md)
**Status:** Backlog

## Description

Intercept signal management functions to coordinate between the shim's internal signal handling (SIGSYS for seccomp, timer signals) and the application's signal handlers. Prevent the application from accidentally breaking the sandbox.

## Deliverables

- `src/shim/signal.c` — interception of:
  - `sigaction(signum, act, oldact)` / `rt_sigaction(...)` — if application installs handler for SIGSYS, chain it after shim's handler (shim handles seccomp traps, then optionally forwards to app). If application installs handler for SIGALRM/SIGVTALRM/SIGPROF, track it for virtual timer delivery.
  - `signal(signum, handler)` — convert to sigaction, apply same logic
  - `sigprocmask(how, set, oldset)` / `rt_sigprocmask(...)` / `pthread_sigmask(...)` — if application tries to block SIGSYS, silently remove SIGSYS from the mask (shim needs SIGSYS for seccomp). Log warning.
  - `kill(pid, sig)` / `tgkill(tgid, tid, sig)` / `tkill(tid, sig)` — pass-through, log for debugging
  - `raise(sig)` / `sigqueue(pid, sig, value)` — pass-through
  - `signalfd(fd, mask, flags)` / `signalfd4(...)` — if mask includes timer signals, integrate with virtual timer delivery
  - `sigaltstack(ss, old_ss)` — coordinate with shim's SIGSYS alt stack. If application sets its own alt stack, shim must ensure SIGSYS handler still has a valid stack.
  - `sigsuspend(mask)` / `sigwait(set, sig)` / `sigwaitinfo(set, info)` — if waiting for timer signals, integrate with virtual timer queue
- Internal signal handler registry:
  - Track application's handlers per signal number
  - Chain shim handlers → application handlers where needed
  - Ensure shim's SIGSYS handler always runs first

## Tests

- Application installs SIGUSR1 handler → works normally, signal delivered
- Application installs SIGALRM handler → shim tracks it, virtual alarm() delivers via this handler
- Application tries to block SIGSYS → mask silently modified, SIGSYS still deliverable
- Application tries to set SIGSYS handler → shim chains it, seccomp still works
- `kill(getpid(), SIGUSR2)` → application's handler called
- `sigaltstack()` by application → seccomp SIGSYS handler still works (shim's stack not clobbered)
- `signalfd(SIGALRM)` → readable when virtual alarm fires
- `sigsuspend()` waiting for SIGALRM → returns when virtual alarm fires
- Signal storm: rapid SIGUSR1 delivery → no crashes, all signals handled
- After all signal interception: seccomp SIGSYS trapping still works correctly

---

[← Back](../story.md)
