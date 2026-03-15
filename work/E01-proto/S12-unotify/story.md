# S12: seccomp_unotify Supervisor

**Epic:** Proto
**Status:** Backlog

## Business Result

Fd-producing syscalls (open, socket, accept) from programs that bypass LD_PRELOAD (Go, static binaries) are fully interceptable. The controller can emulate these syscalls and inject file descriptors into the target process. This closes the last gap in syscall interception coverage.

## Scope

- seccomp_unotify notification fd setup (SECCOMP_FILTER_FLAG_NEW_LISTENER)
- Controller as supervisor: SECCOMP_IOCTL_NOTIF_RECV, SECCOMP_IOCTL_NOTIF_SEND
- Fd injection via SECCOMP_IOCTL_NOTIF_ADDFD (Linux 5.9+)
- Atomic fd injection + response via SECCOMP_ADDFD_FLAG_SEND (Linux 5.14+)
- TOCTOU mitigation via SECCOMP_IOCTL_NOTIF_ID_VALID
- Integration with S04 seccomp filter (RET_TRAP for hot-path, RET_USER_NOTIF for fd-producing)

## Tasks

- [T01: seccomp_unotify supervisor + fd injection](T01-unotify-supervisor/task.md)

## Dependencies

- S04 (seccomp-bpf base infrastructure)
- S02 (controller — supervisor process for handling notifications)

## Acceptance Criteria

- Go program making direct `openat()` syscall → controller intercepts, can redirect to different path
- Go program making direct `socket()` syscall → controller intercepts, can inject fd
- Static binary making `connect()` → controller intercepts, can block or redirect
- Non-intercepted syscalls still pass through at native speed
- RET_TRAP (hot-path) and RET_USER_NOTIF (fd-producing) coexist in same seccomp filter
- Latency overhead for unotify path < 10μs per intercepted call

---

[← Back](../epic.md)
