# T01: Socket Lifecycle + Data Transfer

**Story:** [S08: Network Interception](../story.md)
**Status:** Backlog

## Description

Intercept all socket-related libc functions. For each intercepted call, consult the network policy in shared memory (default: pass-through). Support three policy actions: ALLOW (pass-through with optional latency injection), DENY (return error), ASK_CONTROLLER (IPC to controller for per-call decision).

## Deliverables

- `src/shim/network.c` — interception of:
  - `socket(domain, type, protocol)` — log creation, track fd for later interception
  - `connect(fd, addr, addrlen)` — check policy for destination IP:port. ALLOW → call real + inject latency. DENY → return ECONNREFUSED. ASK_CONTROLLER → IPC.
  - `bind(fd, addr, addrlen)` — log, pass-through (or restrict)
  - `listen(fd, backlog)` — log, pass-through
  - `accept(fd, addr, addrlen)` / `accept4(fd, addr, addrlen, flags)` — log accepted connection, track new fd
  - `shutdown(fd, how)` — clean up tracking
  - `socketpair(domain, type, protocol, sv)` — track both fds
  - `send(fd, buf, len, flags)` / `sendto(...)` / `sendmsg(...)` / `sendmmsg(...)` — pass-through with optional latency injection based on policy
  - `recv(fd, buf, len, flags)` / `recvfrom(...)` / `recvmsg(...)` / `recvmmsg(...)` — pass-through
  - `sendfile(out_fd, in_fd, offset, count)` — pass-through with tracking
  - `setsockopt(fd, level, optname, optval, optlen)` / `getsockopt(...)` — pass-through, log
  - `getpeername(fd, addr, addrlen)` / `getsockname(fd, addr, addrlen)` — pass-through
- `src/common/shm-layout.h` — extend with network policy section:
  - Default action (ALLOW/DENY/ASK)
  - Default latency (microseconds)
  - Per-destination exception table (IP, port, action, latency)
- Socket fd tracking table (which fds are sockets, their remote address)

## Tests

- `socket(AF_INET, SOCK_STREAM, 0)` → creates socket, tracked by shim
- `connect()` with ALLOW policy → succeeds normally
- `connect()` with DENY policy → returns -1, errno=ECONNREFUSED
- `connect()` with latency injection (50ms) → real connect + 50ms delay
- `accept()` → new connection fd tracked
- `send()`/`recv()` on tracked socket → pass-through, logged
- `shutdown()` → fd removed from tracking
- Policy change via shared memory → next connect uses new policy
- Non-socket fds → not affected (write to file still works normally)
- TCP echo server/client under interception → works correctly end-to-end

---

[← Back](../story.md)
