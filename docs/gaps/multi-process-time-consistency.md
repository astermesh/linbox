# Gap: Multi-Process Time Consistency

**Severity:** Low (partially solved)
**Blocks:** Nothing immediately
**Related ADR:** [ADR-002](../adr/002-shm-seqlock-time.md)

## What's Missing

PostgreSQL forks a backend per connection. All backends must see consistent virtual time. The current design (shared memory seqlock) handles the basic case, but edge cases remain:

- **Time advancement during transactions:** If the controller advances time while a transaction is in progress, should the transaction see the new time or the time at transaction start?
- **Ordering guarantees:** If backend A reads time, then backend B reads time, is B guaranteed to see time >= A? (Yes with seqlock, but what about caching?)
- **Fork timing:** A child process forked during a time update — does it see the old or new time? (Seqlock handles this, but needs testing.)

## What's Decided

- All processes read from the same shared memory region
- Seqlock ensures no torn reads
- Controller is the single writer

## Resolution Path

Most of this is likely "just works" with the seqlock design. Needs integration testing with PostgreSQL in E01-S06-T02. Flag specific issues as they arise.

---

[← Back to gaps](README.md)
