# S07: Timer Virtualization

**Epic:** Proto
**Status:** Backlog

## Business Result

Sleeping, timers, and timed waits use virtual time. `nanosleep(5s)` returns when virtual time advances by 5s, not real time. Timer-based wakeups (timerfd, POSIX timers, alarm) fire based on virtual time. Timed blocking calls (`pthread_cond_timedwait`, `poll` with timeout) respect virtual time.

## Scope

- Virtual timer queue in the shim (min-heap or timer wheel)
- Sleep function interception: `nanosleep`, `clock_nanosleep`, `usleep`, `sleep`
- Alarm and interval timers: `alarm`, `setitimer`/`getitimer`
- POSIX per-process timers: `timer_create`, `timer_settime`, `timer_gettime`, `timer_delete`
- Timer file descriptors: `timerfd_create`, `timerfd_settime`, `timerfd_gettime`
- Timed blocking calls: `pthread_cond_timedwait`, `pthread_mutex_timedlock`, `sem_timedwait`
- Timeout parameters in I/O multiplexing: `poll`/`ppoll`, `select`/`pselect` (timeout adjustment)
- Coordination with controller for virtual time advancement

## Tasks

- [T01: Sleep functions + virtual timer queue](T01-sleep-timer-queue/task.md)
- [T02: timerfd + POSIX timers + timed waits](T02-timerfd-posix-timers/task.md)

## Dependencies

- S01 (shim scaffold)
- S02 (controller — virtual time source, time advancement coordination)

## Acceptance Criteria

- `nanosleep(1s)` returns when virtual time advances by 1s
- `timerfd_settime(1s)` fires when virtual time advances by 1s
- `alarm(5)` delivers SIGALRM after 5s virtual time
- `pthread_cond_timedwait` with 2s timeout expires after 2s virtual time
- `pg_sleep(1)` in PostgreSQL returns after 1s virtual time (not 1s real time)
- Timer queue handles 1000+ concurrent timers without degradation

---

[← Back](../epic.md)
