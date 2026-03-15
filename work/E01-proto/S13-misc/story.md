# S13: Miscellaneous System Intercepts

**Epic:** Proto
**Status:** Backlog

## Required Reading

- [LD_PRELOAD interception — miscellaneous functions](../../rnd/boxing/ld-preload.md)
- [LinBox architecture — miscellaneous section](../../rnd/boxing/linux-sandbox.md)

## Business Result

System identity and resource usage reflect the virtual environment. `uname()` returns a virtual hostname, `sysinfo()` returns virtual uptime and load, `getrusage()` returns virtual CPU times. The sandbox is indistinguishable from a real system for these calls.

## Scope

- `uname()` — virtual nodename, domainname
- `sysinfo()` — virtual uptime (derived from virtual time), load averages, memory info
- `gethostname()` / `sethostname()` — virtual hostname
- `getrusage()` — virtual user/system CPU time (consistent with virtual CLOCK_PROCESS_CPUTIME_ID)
- `ioctl()` — selective interception (terminal ioctls, etc.)
- `prctl()` — selective interception (PR_SET_NAME for process naming)

## Tasks

- [T01: System info interception](T01-system-info/task.md)

## Dependencies

- S01 (shim scaffold)
- S02 (controller — virtual hostname and identity configuration)

## Acceptance Criteria

- `uname()` returns controller-configured hostname
- `sysinfo()` returns uptime consistent with virtual time
- `gethostname()` returns virtual hostname
- `getrusage()` returns CPU times consistent with virtual clock
- Non-intercepted ioctls pass through normally

---

[← Back](../epic.md)
