# T02: timerfd + POSIX Timers + Timed Waits

**Story:** [S07: Timer Virtualization](../story.md)
**Status:** Backlog

## Description

Intercept timer file descriptors, POSIX per-process timers, and timed blocking calls. timerfd is heavily used by event loops (libuv, libev, epoll-based servers). POSIX timers are used by PostgreSQL and other services. Timed waits (pthread_cond_timedwait, sem_timedwait) must use virtual time for their deadlines.

## Deliverables

- `src/shim/timerfd.c` — interception of:
  - `timerfd_create(clockid, flags)` — create a virtual timerfd backed by the timer queue. Return a real eventfd or pipe fd that the shim controls (so epoll/poll on timerfd works). Track the association between fd and virtual timer.
  - `timerfd_settime(fd, flags, new, old)` — register/update virtual timer. When timer fires, write to the backing fd to trigger epoll/poll readability. Handle TFD_TIMER_ABSTIME.
  - `timerfd_gettime(fd, curr)` — return remaining virtual time until expiration
- `src/shim/posix-timer.c` — interception of:
  - `timer_create(clockid, sevp, timerid)` — create virtual POSIX timer. Support SIGEV_SIGNAL notification.
  - `timer_settime(timerid, flags, new, old)` — arm with virtual time deadline. Handle TIMER_ABSTIME.
  - `timer_gettime(timerid, curr)` — return remaining virtual time
  - `timer_delete(timerid)` — cancel and clean up
  - `timer_getoverrun(timerid)` — return overrun count for interval timers
- `src/shim/timed-wait.c` — interception of:
  - `pthread_cond_timedwait(cond, mutex, abstime)` — convert virtual abstime to real abstime (or use timer queue to enforce virtual deadline)
  - `pthread_mutex_timedlock(mutex, abstime)` — same approach
  - `sem_timedwait(sem, abstime)` — same approach
  - `sigtimedwait(set, info, timeout)` — wait for signal with virtual timeout
- Adjust timeout parameters in I/O multiplexing (preparation for S08):
  - `poll()` / `ppoll()` timeout → convert virtual to real
  - `select()` / `pselect()` timeout → convert virtual to real
  - `epoll_wait()` / `epoll_pwait()` timeout → convert virtual to real

## Tests

- `timerfd_create(CLOCK_REALTIME)` + `timerfd_settime(1s)` + `epoll_wait()` → epoll returns after 1s virtual time
- `timerfd_settime` with TFD_TIMER_ABSTIME → fires at absolute virtual time
- `timerfd_gettime` → returns correct remaining virtual time
- Interval timerfd (1s interval) → fires repeatedly every 1s virtual time
- `timer_create(CLOCK_REALTIME, SIGEV_SIGNAL)` + `timer_settime(1s)` → signal delivered after 1s virtual time
- `timer_gettime` → correct remaining time
- `timer_delete` → no more signals
- `pthread_cond_timedwait` with 2s virtual timeout → returns ETIMEDOUT after 2s virtual time
- `sem_timedwait` with 1s virtual timeout → returns ETIMEDOUT after 1s virtual time
- `poll(fds, 1, 5000)` → times out after 5s virtual time
- `select` with 3s timeout → times out after 3s virtual time
- `epoll_wait(epfd, events, 10, 2000)` → times out after 2s virtual time
- timerfd readable via poll/select/epoll — all three I/O multiplexing methods work with virtual timerfd

---

[← Back](../story.md)
