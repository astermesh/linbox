# T01: Sleep Functions + Virtual Timer Queue

**Story:** [S07: Timer Virtualization](../story.md)
**Status:** Backlog

## Description

Implement the virtual timer queue and intercept sleep-related functions. When a process calls `nanosleep(5s)`, the shim registers a timer with the controller and blocks the calling thread until virtual time advances past the deadline.

## Deliverables

- `src/shim/timer-queue.h` / `src/shim/timer-queue.c` — virtual timer queue:
  - Min-heap ordered by virtual deadline
  - Register timer: `tq_add(deadline_ns, callback, userdata)` → timer_id
  - Cancel timer: `tq_cancel(timer_id)`
  - Process expired: `tq_fire(current_virtual_time_ns)` — fires all expired timers
  - Thread-safe (multiple threads can register timers concurrently)
- `src/shim/sleep.c` — interception of:
  - `nanosleep(req, rem)` — register timer at `virtual_now + req`, block thread via futex/condvar, wake when timer fires, fill `rem` with remaining virtual time if interrupted
  - `clock_nanosleep(clk, flags, req, rem)` — same, with TIMER_ABSTIME support (absolute virtual deadline)
  - `usleep(usec)` — delegate to nanosleep logic
  - `sleep(seconds)` — delegate to nanosleep logic
  - `alarm(seconds)` — register timer, deliver SIGALRM to process when virtual time expires. Return seconds remaining on previous alarm.
  - `setitimer(which, new, old)` / `getitimer(which, value)` — interval timers (ITIMER_REAL → virtual time, ITIMER_VIRTUAL/ITIMER_PROF → virtual CPU time)
- Controller integration:
  - Shim sends REGISTER_TIMER / CANCEL_TIMER SBP messages to controller
  - Controller tracks all pending timers across all processes
  - When controller advances virtual time, it notifies shim via eventfd/shared memory flag
  - Shim's timer thread checks queue, wakes blocked threads

## Tests

- `nanosleep({.tv_sec=1})` with virtual time frozen → blocks until controller advances time by 1s
- `nanosleep({.tv_sec=1})` with virtual time advancing → returns after 1s virtual time
- `sleep(5)` → returns after 5s virtual time
- `usleep(500000)` → returns after 500ms virtual time
- `clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &abs_time, NULL)` → returns when virtual time reaches abs_time
- `clock_nanosleep(CLOCK_MONOTONIC, 0, &rel, NULL)` → monotonic-based relative sleep
- `alarm(3)` → SIGALRM delivered after 3s virtual time
- `alarm(5)` then `alarm(3)` → first alarm canceled, returns 5, SIGALRM after 3s
- `setitimer(ITIMER_REAL, 100ms interval)` → repeated SIGALRM every 100ms virtual time
- Concurrent: 100 threads each sleeping for different durations → all wake at correct virtual times
- Interrupted sleep: signal delivered during nanosleep → returns -1/EINTR with correct `rem`
- Timer queue with 1000 pending timers → fire in correct order

---

[← Back](../story.md)
