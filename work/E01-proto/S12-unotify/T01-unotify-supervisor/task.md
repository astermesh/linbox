# T01: seccomp_unotify Supervisor + Fd Injection

**Story:** [S12: seccomp_unotify Supervisor](../story.md)
**Status:** Backlog

## Description

Implement the cross-process seccomp supervisor in the controller. The shim installs a seccomp filter with SECCOMP_FILTER_FLAG_NEW_LISTENER, passes the notification fd to the controller. The controller receives syscall notifications, makes decisions, and responds with spoofed return values or injected file descriptors.

## Deliverables

- `src/shim/seccomp-unotify.c` — notification fd setup:
  - Install seccomp filter with SECCOMP_FILTER_FLAG_NEW_LISTENER for fd-producing syscalls (openat, socket, accept4, connect)
  - Pass notification fd to controller via SCM_RIGHTS over unix socket
  - Coexist with existing SECCOMP_RET_TRAP filter (S04) — compose filters correctly (RET_TRAP for hot-path like clock_gettime, RET_USER_NOTIF for fd-producing)
- `src/controller/unotify-handler.c` — notification handler:
  - Event loop for notification fd: `ioctl(fd, SECCOMP_IOCTL_NOTIF_RECV, &notif)`
  - Check TOCTOU validity: `ioctl(fd, SECCOMP_IOCTL_NOTIF_ID_VALID, &id)` before reading pointer arguments via `/proc/pid/mem`
  - Decision logic per syscall:
    - `openat()` — check path (read via process_vm_readv), apply fs policy. Option to redirect to different path, or open on behalf and inject fd.
    - `socket()` — check domain/type, apply network policy. Option to deny or create and inject fd.
    - `accept4()` — log accepted connection, inject fd
    - `connect()` — check destination, apply network policy
  - Response: `ioctl(fd, SECCOMP_IOCTL_NOTIF_SEND, &resp)` with spoofed return or SECCOMP_USER_NOTIF_FLAG_CONTINUE
  - Fd injection: `ioctl(fd, SECCOMP_IOCTL_NOTIF_ADDFD, &addfd)` with SECCOMP_ADDFD_FLAG_SEND for atomic inject+respond
- SBP messages: UNOTIFY_READY (shim → controller with notification fd)

## Tests

- Go test program making direct `openat("/etc/hostname", ...)` → controller intercepts, can redirect to "/tmp/fake-hostname"
- Go test program making direct `socket(AF_INET, SOCK_STREAM, 0)` → controller intercepts, returns fd
- Controller blocks `socket(AF_INET6, ...)` → target gets EACCES
- Controller allows `openat()` to proceed (CONTINUE flag) → real syscall executes
- Fd injection: controller opens a file, injects fd into target → target reads from injected fd
- TOCTOU check: verify notification is still valid before responding
- Mixed filter: clock_gettime goes through RET_TRAP (fast path), openat goes through RET_USER_NOTIF (supervisor)
- Latency measurement: unotify round-trip < 10μs
- Stress: 1000 rapid openat calls from Go program → all intercepted, no dropped notifications
- Multiple concurrent targets: controller handles notifications from multiple sandboxed processes

---

[← Back](../story.md)
