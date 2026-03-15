# T02: DNS Resolution + I/O Multiplexing

**Story:** [S08: Network Interception](../story.md)
**Status:** Backlog

## Description

Intercept DNS resolution to return controlled addresses. Intercept I/O multiplexing functions (poll, select, epoll) for logging and virtual timeout support.

## Deliverables

- `src/shim/dns.c` — interception of:
  - `getaddrinfo(node, service, hints, res)` — check DNS override table in shared memory. If match, return controlled address. Otherwise, call real getaddrinfo (or forward to controller).
  - `gethostbyname(name)` / `gethostbyname2(name, af)` — same logic, legacy API
  - `gethostbyaddr(addr, len, type)` — reverse DNS, check override table
  - `getifaddrs(ifap)` — return controlled interface list if policy says so
  - `if_nametoindex(ifname)` — pass-through
- `src/common/shm-layout.h` — extend with DNS policy section:
  - Hostname → IP override table (e.g., "db.internal" → 10.0.0.5)
- `src/shim/io-mux.c` — interception of:
  - `poll(fds, nfds, timeout)` / `ppoll(fds, nfds, timeout, sigmask)` — adjust timeout from virtual to real (if timer virtualization active), pass-through to real function
  - `select(nfds, readfds, writefds, exceptfds, timeout)` / `pselect(...)` — same timeout adjustment
  - `epoll_create()` / `epoll_create1(flags)` — pass-through, track epoll fd
  - `epoll_ctl(epfd, op, fd, event)` — pass-through
  - `epoll_wait(epfd, events, maxevents, timeout)` / `epoll_pwait(...)` / `epoll_pwait2(...)` — adjust timeout

## Tests

- `getaddrinfo("fake.local")` with override "fake.local" → 10.0.0.99 → returns 10.0.0.99
- `getaddrinfo("real.com")` without override → returns real DNS result
- `gethostbyname("fake.local")` → returns controlled address
- DNS override table updated via shared memory → new lookups use new mapping
- `poll()` with 5s timeout → times out after 5s virtual time (when timer virtualization active)
- `select()` with 3s timeout → same behavior
- `epoll_wait()` with 2s timeout → same behavior
- `epoll_create` + `epoll_ctl(ADD)` + `epoll_wait` on a connected socket → events returned correctly
- Event-driven server under interception (epoll loop) → serves requests normally

---

[← Back](../story.md)
