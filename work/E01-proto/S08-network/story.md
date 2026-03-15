# S08: Network Interception

**Epic:** Proto
**Status:** Backlog

## Required Reading

- [LD_PRELOAD interception — network functions](../../rnd/boxing/ld-preload.md)
- [Sandbox alternatives — network interception](../../rnd/boxing/sandbox-alternatives.md)
- [ADR-010: tc/netem](../../docs/adr/010-tc-netem-network.md)

## Business Result

All network syscalls are interceptable. The shim can log, delay, modify, or block any network operation. DNS resolution returns controlled addresses. I/O multiplexing works with virtual timeouts.

## Scope

- Socket lifecycle: `socket`, `connect`, `bind`, `listen`, `accept`/`accept4`, `shutdown`, `socketpair`
- Data transfer: `send`/`sendto`/`sendmsg`/`sendmmsg`, `recv`/`recvfrom`/`recvmsg`/`recvmmsg`, `sendfile`
- Socket info: `setsockopt`/`getsockopt`, `getpeername`/`getsockname`
- DNS resolution: `getaddrinfo`, `gethostbyname`/`gethostbyname2`/`gethostbyaddr`, `getifaddrs`, `if_nametoindex`
- I/O multiplexing: `poll`/`ppoll`, `select`/`pselect`, `epoll_create`/`epoll_create1`/`epoll_ctl`/`epoll_wait`/`epoll_pwait`/`epoll_pwait2`
- Network policy resolution from shared memory (default action, per-destination rules)

## Tasks

- [T01: Socket lifecycle + data transfer](T01-socket-lifecycle/task.md)
- [T02: DNS resolution + I/O multiplexing](T02-dns-io-mux/task.md)

## Dependencies

- S01 (shim scaffold)
- S02 (controller — network policies in shared memory)
- S07 (timer virtualization — timeout handling in poll/select/epoll)

## Acceptance Criteria

- `connect()` to any destination can be logged, delayed, or blocked via policy
- `getaddrinfo("example.com")` returns controller-specified address
- `epoll_wait` with timeout respects virtual time
- Network policy changes take effect without restarting the sandboxed process
- Sandboxed process can serve and consume TCP connections normally when policy allows

---

[← Back](../epic.md)
