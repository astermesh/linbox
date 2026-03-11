# Gap: Virtual Timer Queue Design

**Severity:** Medium
**Blocks:** Phase 2 (not needed for proto)
**Related ADR:** [ADR-002](../adr/002-shm-seqlock-time.md)

## What's Missing

When virtual time advances, sleeping processes must wake up at the correct virtual time. This requires a virtual timer queue:

- How to manage thousands of concurrent timers (PostgreSQL backends each with their own `pg_sleep`, `statement_timeout`, etc.)
- Data structure for the timer queue (min-heap? timer wheel?)
- Interaction with the event loop — when virtual time advances, which timers fire?
- `timerfd_create/settime/gettime` — these return real file descriptors. Virtual timers need to signal via these fds.
- `nanosleep` and `clock_nanosleep` — must return when virtual time reaches target, not real time
- `alarm` and `setitimer` — signal-based timers

## What's Decided

- Timer virtualization is Phase 2 (explicit non-goal for proto)
- Proto phase: `pg_sleep(1)` will actually sleep for 1 real second

## Resolution Path

Design the timer queue as part of Phase 2 planning. Key reference: Shadow's timer implementation. Consider whether the controller or the shim manages the queue.

---

[← Back to gaps](README.md)
