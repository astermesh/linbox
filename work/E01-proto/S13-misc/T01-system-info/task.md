# T01: System Info Interception

**Story:** [S13: Miscellaneous System Intercepts](../story.md)
**Status:** Backlog

## Description

Intercept system identification and resource usage functions to return virtual values consistent with the simulated environment.

## Deliverables

- `src/shim/sysinfo.c` — interception of:
  - `uname(buf)` — call real uname, then overwrite `buf->nodename` and `buf->domainname` with values from shared memory policy. Keep sysname ("Linux"), release, version, machine from real system.
  - `sysinfo(info)` — call real sysinfo, then overwrite: `info->uptime` = virtual_time - boot_time, `info->loads[]` = configured virtual load averages. Keep real totalram/freeram/procs.
  - `gethostname(name, len)` — return virtual hostname from shared memory
  - `sethostname(name, len)` — update virtual hostname in shared memory (or pass-through if policy allows)
  - `getrusage(who, usage)` — call real getrusage, then overwrite `ru_utime` and `ru_stime` with values derived from virtual CLOCK_PROCESS_CPUTIME_ID. This ensures consistency when programs compare getrusage with clock_gettime(CLOCK_PROCESS_CPUTIME_ID).
  - `ioctl(fd, request, ...)` — selective: for TIOCGWINSZ (terminal size) and similar, pass-through. Log others for debugging.
  - `prctl(option, ...)` — selective: PR_SET_NAME (process name) → pass-through. PR_SET_SECCOMP → block if shim's filter is already installed (prevent application from modifying seccomp state).
- `src/common/shm-layout.h` — extend with system identity section:
  - Virtual hostname (64 bytes)
  - Virtual domainname (64 bytes)
  - Boot time (for uptime calculation)
  - Virtual load averages (3 doubles)

## Tests

- `uname(&buf)` → buf.nodename matches controller-configured hostname
- `gethostname()` → returns virtual hostname
- `sysinfo()` → uptime consistent with virtual time (if virtual time is 2025-01-01 and boot is 2025-01-01, uptime=0)
- Controller advances virtual time by 3600s → sysinfo uptime = 3600
- `getrusage(RUSAGE_SELF)` → ru_utime consistent with clock_gettime(CLOCK_PROCESS_CPUTIME_ID)
- `prctl(PR_SET_NAME, "worker")` → works normally
- `prctl(PR_SET_SECCOMP, ...)` by application → blocked (shim's seccomp filter protected)
- `ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws)` → works normally (terminal size pass-through)

---

[← Back](../story.md)
