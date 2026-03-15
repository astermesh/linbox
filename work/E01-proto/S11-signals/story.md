# S11: Signal Management

**Epic:** Proto
**Status:** Backlog

## Business Result

Signal delivery is controlled. Timer-related signals (SIGALRM, SIGVTALRM, SIGPROF) are delivered by the shim based on virtual time. The shim's internal signal handlers (SIGSYS for seccomp) coexist with application signal handlers without conflicts.

## Scope

- Signal handler interception: `sigaction`/`rt_sigaction`, `signal` — track application handlers, chain with shim's internal handlers
- Signal mask management: `sigprocmask`/`rt_sigprocmask`, `pthread_sigmask` — prevent application from blocking shim-critical signals (SIGSYS)
- Signal delivery: `kill`/`tgkill`/`tkill`, `raise`, `sigqueue` — log, pass-through
- Signal fd: `signalfd`/`signalfd4` — handle virtual timer signals via signalfd
- Signal stack: `sigaltstack` — coordinate with seccomp SIGSYS handler's alt stack
- Signal waiting: `sigsuspend`, `sigwait`, `sigwaitinfo` — respect virtual time for timer signals

## Tasks

- [T01: Signal interception](T01-signal-interception/task.md)

## Dependencies

- S01 (shim scaffold)
- S04 (seccomp — SIGSYS handler coexistence)
- S07 (timer virtualization — timer signal delivery)

## Acceptance Criteria

- Application installs SIGALRM handler → shim chains it with virtual timer delivery
- Application does not block SIGSYS (shim prevents it)
- `kill(getpid(), SIGUSR1)` → application's handler called normally
- `signalfd` for SIGALRM → readable when virtual timer fires
- Shim's SIGSYS handler and application's signal handlers coexist without interference

---

[← Back](../epic.md)
