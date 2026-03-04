# R10: LinuxBox — Fully Hookable Linux Sandbox

Comprehensive research on building a Linux sandbox where **every** outbound call (time, network, filesystem, random, signals) is interceptable and controllable. Full determinism. Full time control. Real services, simulated physics.

## Problem Statement

LinBox needs a way to run real native services (PostgreSQL, Redis, Nginx, Node.js) inside a controlled environment where the Sim can intercept all OBI (Outbound Boundary Interface) points — the same way WASM import interception works for in-process boxes, but for real Linux processes.

**Goal:** A container where the service inside believes it talks to a real OS, but every syscall that touches the outside world is hookable by LinBox.

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [Interception Points — Complete Map](#2-interception-points--complete-map)
3. [Interception Mechanisms](#3-interception-mechanisms)
4. [Recommended Architecture: Layered Stack](#4-recommended-architecture-layered-stack)
5. [The liblinbox.so Shim — Design](#5-the-liblinboxso-shim--design)
6. [Edge Cases and Bypasses](#6-edge-cases-and-bypasses)
7. [Prior Art — What Works in Production](#7-prior-art--what-works-in-production)
8. [Cost Estimation](#8-cost-estimation)
9. [Implementation Roadmap](#9-implementation-roadmap)
10. [Open Questions](#10-open-questions)

---

## 1. Architecture Overview

```
┌──────────────────────── LinuxBox ────────────────────────────┐
│                                                               │
│  ┌─────────────────────────────────────────────────────────┐  │
│  │  Layer 4: Protocol-Level (IBI)                          │  │
│  │  TPROXY / eBPF proxy for wire protocol interception     │  │
│  │  (Postgres wire, Redis RESP, HTTP)                      │  │
│  ├─────────────────────────────────────────────────────────┤  │
│  │  Layer 3: Syscall Interception (OBI)                    │  │
│  │  Primary: LD_PRELOAD (liblinbox.so) — ~100ns/call       │  │
│  │  Fallback: seccomp SECCOMP_RET_TRAP — ~800ns/call       │  │
│  │  Safety net: seccomp SECCOMP_RET_USER_NOTIF — ~3μs/call │  │
│  ├─────────────────────────────────────────────────────────┤  │
│  │  Layer 2: Network & Resource Control                    │  │
│  │  veth pairs + tc/netem + cgroups + iptables             │  │
│  ├─────────────────────────────────────────────────────────┤  │
│  │  Layer 1: Isolation (Kernel Primitives)                 │  │
│  │  PID ns + network ns + mount ns + user ns + timens      │  │
│  ├─────────────────────────────────────────────────────────┤  │
│  │  Layer 0: Container Runtime (Docker/Podman)             │  │
│  └─────────────────────────────────────────────────────────┘  │
│                              ▲                                │
│                              │ SBP (LinBox Protocol)          │
│                         unix socket / shared memory           │
└──────────────────────────────┼────────────────────────────────┘
                               │
                    ┌──────────▼──────────┐
                    │   LinBox Controller  │
                    │   (Sim + Law)        │
                    └─────────────────────┘
```

The key insight: **no single mechanism covers everything**. The architecture is layered — each layer handles what it does best, and the combination provides 100% coverage.

---

## 2. Interception Points — Complete Map

Every point where a Linux process touches the outside world, categorized by OBI type.

### 2.1 Time

All calls that return current time or control time-based operations.

| Syscall / Function | What It Does | Interception Strategy |
|---|---|---|
| `clock_gettime(CLOCK_REALTIME)` | Wall-clock time (ns precision) | LD_PRELOAD + shared memory (hot path) |
| `clock_gettime(CLOCK_MONOTONIC)` | Monotonic time | LD_PRELOAD + timens offset (free) |
| `clock_gettime(CLOCK_BOOTTIME)` | Boot-relative time | LD_PRELOAD + timens offset (free) |
| `clock_gettime(CLOCK_MONOTONIC_RAW)` | Hardware monotonic | LD_PRELOAD |
| `clock_gettime(CLOCK_PROCESS_CPUTIME_ID)` | Per-process CPU time | LD_PRELOAD |
| `clock_gettime(CLOCK_THREAD_CPUTIME_ID)` | Per-thread CPU time | LD_PRELOAD |
| `gettimeofday()` | Wall-clock time (μs precision) | LD_PRELOAD + shared memory |
| `time()` | Wall-clock time (s precision) | LD_PRELOAD + shared memory |
| `clock_getres()` | Clock resolution | LD_PRELOAD |
| `clock_nanosleep()` | High-precision sleep | LD_PRELOAD → virtual timer queue |
| `nanosleep()` | Sleep with ns precision | LD_PRELOAD → virtual timer queue |
| `usleep()` | Sleep with μs precision | LD_PRELOAD → virtual timer queue |
| `sleep()` | Sleep with s precision | LD_PRELOAD → virtual timer queue |
| `timer_create()` | POSIX per-process timer | LD_PRELOAD → virtual timer queue |
| `timer_settime()` | Arm/disarm POSIX timer | LD_PRELOAD → virtual timer queue |
| `timerfd_create()` | Timer via file descriptor | LD_PRELOAD → virtual timer queue |
| `timerfd_settime()` | Arm/disarm timerfd | LD_PRELOAD → virtual timer queue |
| `timerfd_gettime()` | Get remaining time | LD_PRELOAD |
| `alarm()` | Set alarm signal | LD_PRELOAD |
| `setitimer()` / `getitimer()` | Interval timers | LD_PRELOAD |

**vDSO note:** `clock_gettime`, `gettimeofday`, and `time` are accelerated via vDSO (kernel maps code into userspace, no actual syscall). LD_PRELOAD intercepts the libc wrapper **before** it calls the vDSO, so this works. Direct vDSO calls (bypassing libc) are extremely rare but possible — handled by the seccomp fallback layer.

**Total: ~20 interception points for time.**

### 2.2 Randomness

| Syscall / Function | What It Does | Interception Strategy |
|---|---|---|
| `getrandom(buf, len, flags)` | Kernel random bytes | LD_PRELOAD → seeded PRNG |
| `read(/dev/urandom)` | Random bytes via device | FUSE mount-over or LD_PRELOAD `open` redirect |
| `read(/dev/random)` | Blocking random bytes | Same as above |
| `getauxval(AT_RANDOM)` | 16 bytes from kernel at exec | Overwrite in shim constructor (Shadow approach) |
| `RDRAND` / `RDSEED` (x86) | Hardware random instruction | Trap via CPUID masking (Shadow) or seccomp |
| `arc4random()` | libc random (uses getrandom) | LD_PRELOAD |

**Total: ~6 interception points for randomness.**

### 2.3 Network

| Syscall / Function | What It Does | Interception Strategy |
|---|---|---|
| `socket()` | Create socket | LD_PRELOAD or seccomp notifier |
| `connect()` | Connect to remote | LD_PRELOAD + network namespace |
| `bind()` | Bind to address | LD_PRELOAD |
| `listen()` | Listen for connections | LD_PRELOAD |
| `accept()` / `accept4()` | Accept connection | LD_PRELOAD |
| `send()` / `sendto()` / `sendmsg()` | Send data | LD_PRELOAD + tc/netem for latency |
| `recv()` / `recvfrom()` / `recvmsg()` | Receive data | LD_PRELOAD + tc/netem |
| `shutdown()` | Shutdown socket | LD_PRELOAD |
| `getsockopt()` / `setsockopt()` | Socket options | LD_PRELOAD |
| `getpeername()` / `getsockname()` | Socket addresses | LD_PRELOAD |
| `poll()` / `ppoll()` | Wait for I/O events | LD_PRELOAD |
| `select()` / `pselect6()` | Wait for I/O events | LD_PRELOAD |
| `epoll_create()` / `epoll_ctl()` / `epoll_wait()` | Epoll I/O multiplexing | LD_PRELOAD |
| `getaddrinfo()` / `gethostbyname()` | DNS resolution | LD_PRELOAD → custom resolver |
| `sendfile()` | Zero-copy file-to-socket | LD_PRELOAD |

**Network namespace provides topology isolation for free.** tc/netem adds latency, loss, and bandwidth control with near-zero overhead. Per-destination rules are possible.

**Total: ~20 interception points for network.**

### 2.4 Filesystem

| Syscall / Function | What It Does | Interception Strategy |
|---|---|---|
| `open()` / `openat()` | Open file | LD_PRELOAD (selective) or FUSE |
| `read()` / `pread64()` / `readv()` | Read from fd | LD_PRELOAD (for specific fds) |
| `write()` / `pwrite64()` / `writev()` | Write to fd | LD_PRELOAD (for specific fds) |
| `close()` | Close fd | LD_PRELOAD |
| `stat()` / `fstat()` / `lstat()` / `statx()` | File metadata | LD_PRELOAD (timestamps!) |
| `access()` / `faccessat()` | Check permissions | LD_PRELOAD |
| `mkdir()` / `mkdirat()` | Create directory | LD_PRELOAD or pass-through |
| `unlink()` / `unlinkat()` | Delete file | LD_PRELOAD |
| `rename()` / `renameat()` | Rename file | LD_PRELOAD |
| `readdir()` / `getdents64()` | List directory | LD_PRELOAD |
| `fsync()` / `fdatasync()` | Flush to disk | LD_PRELOAD → inject delay |
| `ftruncate()` | Truncate file | LD_PRELOAD |
| `mmap()` (file-backed) | Memory-map file | Complex — may need FUSE |
| `lseek()` | Seek in file | LD_PRELOAD |

**Mount namespace + overlay filesystem** provides filesystem isolation. FUSE can intercept all filesystem operations at 2-4x latency overhead if fine-grained control is needed.

**Important:** File timestamps (`stat.st_mtime`, etc.) must reflect virtual time. libfaketime already intercepts `fstat` for this. The shim must do the same.

**Total: ~20 interception points for filesystem.**

### 2.5 Process

| Syscall / Function | What It Does | Interception Strategy |
|---|---|---|
| `fork()` / `vfork()` / `clone()` / `clone3()` | Create process/thread | seccomp + shim inheritance |
| `execve()` / `execveat()` | Replace process image | seccomp (re-inject LD_PRELOAD) |
| `getpid()` / `getppid()` | Process IDs | PID namespace (free) |
| `gettid()` | Thread ID | PID namespace (free) |
| `getuid()` / `getgid()` / `geteuid()` / `getegid()` | User/group IDs | User namespace (free) |
| `wait4()` / `waitid()` / `waitpid()` | Wait for child | LD_PRELOAD |
| `exit()` / `exit_group()` | Exit process | Notify controller |
| `prctl()` | Process control | LD_PRELOAD (selective) |
| `sched_yield()` / `sched_getaffinity()` | Scheduling | LD_PRELOAD |

**PID namespace and user namespace handle most process identity virtualization for free** — no interception overhead.

**Critical: `fork()` and `clone()`** — child processes inherit LD_PRELOAD, but each new process needs its own OBI state (own virtual time offset, own random seed). The shim must detect fork and initialize per-process state.

**Total: ~15 interception points for process.**

### 2.6 Signals

| Syscall / Function | What It Does | Interception Strategy |
|---|---|---|
| `rt_sigaction()` | Set signal handler | LD_PRELOAD (intercept timer signals) |
| `rt_sigprocmask()` | Block/unblock signals | LD_PRELOAD |
| `kill()` / `tgkill()` | Send signal | LD_PRELOAD |
| `sigaltstack()` | Set signal stack | LD_PRELOAD (needed for SIGSYS handler) |
| `rt_sigreturn()` | Return from signal | Pass-through |
| `signalfd()` / `signalfd4()` | Signal via fd | LD_PRELOAD |

**Total: ~8 interception points for signals.**

### 2.7 Memory

| Syscall / Function | What It Does | Interception Strategy |
|---|---|---|
| `mmap()` / `munmap()` | Map/unmap memory | Pass-through (usually) |
| `mprotect()` | Change memory protection | Pass-through |
| `brk()` | Change data segment | Pass-through |
| `mremap()` | Remap memory | Pass-through |
| `madvise()` | Memory usage hints | Pass-through |

**Memory syscalls are generally pass-through** — they don't touch the "outside world" in a way that affects simulation. Exception: file-backed `mmap` may need interception for filesystem simulation.

### 2.8 Miscellaneous

| Syscall / Function | What It Does | Interception Strategy |
|---|---|---|
| `uname()` | System identification | LD_PRELOAD → return virtual hostname |
| `sysinfo()` | System statistics | LD_PRELOAD → virtual uptime, load |
| `ioctl()` | Device control | LD_PRELOAD (selective — e.g., terminal) |
| `futex()` | Fast userspace locking | Pass-through (usually) |

### Summary

| OBI Category | Interception Points | Hot Path? | Primary Mechanism |
|---|---|---|---|
| **Time** | ~20 | Yes (`clock_gettime` called millions of times) | LD_PRELOAD + shared memory |
| **Randomness** | ~6 | Medium | LD_PRELOAD + seeded PRNG |
| **Network** | ~20 | Yes (data transfer) | LD_PRELOAD + network namespace + tc/netem |
| **Filesystem** | ~20 | Yes (database I/O) | LD_PRELOAD + mount namespace + optional FUSE |
| **Process** | ~15 | Low | PID/user namespace (free) + seccomp for fork |
| **Signals** | ~8 | Low | LD_PRELOAD |
| **Memory** | ~5 | N/A | Pass-through |
| **TOTAL** | **~94** | | |

---

## 3. Interception Mechanisms

### 3.1 LD_PRELOAD — Fast Path (~100ns overhead)

The dynamic linker loads `liblinbox.so` before libc. The shim defines functions with the same signatures as libc wrappers. When the application calls `clock_gettime()`, it gets our version.

```c
// liblinbox.so — simplified
int clock_gettime(clockid_t clk_id, struct timespec *tp) {
    if (clk_id == CLOCK_REALTIME) {
        // Read virtual time from shared memory — zero IPC
        *tp = shared_state->virtual_realtime;
        return 0;
    }
    // Fallback to real syscall
    return real_clock_gettime(clk_id, tp);
}
```

**Strengths:**
- Zero overhead for hot path (shared memory for time, in-process for everything)
- Full access to arguments, memory, and return values
- Well-understood, used by libfaketime, fakeroot, proxychains, Shadow

**Weaknesses:**
- Bypassed by: static binaries, Go runtime, direct `syscall` instruction, io_uring
- Doesn't work with setuid binaries
- Requires dynamic linking

### 3.2 seccomp-bpf with SECCOMP_RET_TRAP — Safety Net (~800ns overhead)

For syscalls that bypass LD_PRELOAD, a seccomp filter triggers SIGSYS. The signal handler (in the shim) handles the syscall virtually.

```c
// seccomp filter: if instruction pointer is NOT in liblinbox.so, trap
BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_clock_gettime, 0, 1),
BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),  // SIGSYS
BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
```

This is the approach **gVisor Systrap** uses. Overhead is ~800ns per trapped syscall (signal dispatch + handler execution). gVisor further optimizes by patching known `mov+syscall` patterns with `jmp` instructions.

### 3.3 seccomp_unotify (SECCOMP_RET_USER_NOTIF) — Cross-Process (~3-7μs overhead)

For complex cases requiring the LinBox controller to make decisions (e.g., fd injection, supervisor-level operations):

- Syscall pauses, notification sent to controller process
- Controller responds with spoofed return value or injected fd
- Since Linux 5.9: `SECCOMP_IOCTL_NOTIF_ADDFD` for fd injection
- Since Linux 5.14: Atomic fd injection + response

**Not for hot paths.** Reserved for infrequent operations like `open()`, `connect()`, or `mount()` when fine-grained control is needed.

### 3.4 Kernel Primitives — Free Isolation

| Mechanism | What It Provides | Overhead |
|---|---|---|
| PID namespace | Virtual PIDs (getpid returns 1) | Zero |
| Network namespace | Isolated network stack | Zero |
| Mount namespace | Isolated filesystem view | Zero |
| User namespace | Virtual UID/GID mapping | Zero |
| Time namespace | CLOCK_MONOTONIC/BOOTTIME offsets | Zero |
| cgroups v2 | CPU/memory/IO throttling | Near-zero |
| tc/netem | Network latency/loss/jitter | Near-zero |
| iptables/nftables | Network partitions | Near-zero |

### 3.5 Mechanism Selection Flowchart

```
Syscall arrives
    │
    ├── Is libc wrapper used? (dynamically linked, calls libc)
    │   └── YES → LD_PRELOAD intercepts (~100ns) ✓
    │
    ├── Direct syscall instruction? (Go, static, inline asm)
    │   └── YES → seccomp SECCOMP_RET_TRAP → SIGSYS handler (~800ns) ✓
    │
    ├── io_uring submission?
    │   └── YES → Block io_uring_setup via seccomp ✓ (force fallback to regular I/O)
    │
    ├── Network operation?
    │   └── YES → Network namespace + tc/netem (zero to near-zero overhead) ✓
    │
    └── Needs supervisor decision? (complex fd operations)
        └── YES → seccomp_unotify → controller (~3-7μs) ✓
```

---

## 4. Recommended Architecture: Layered Stack

### Layer 0: Container Runtime

Standard Docker/Podman container. Provides:
- Image management (pull `postgres:17`, `redis:7`, etc.)
- Namespace creation (PID, network, mount, user, UTS, IPC)
- Cgroup setup
- Volume mounting

```bash
docker run \
  --cap-add SYS_PTRACE \
  --cap-add NET_ADMIN \
  --security-opt seccomp=linbox-seccomp.json \
  -e LD_PRELOAD=/usr/lib/liblinbox.so \
  -e LINBOX_CONTROLLER=unix:///var/run/linbox.sock \
  -v /var/run/linbox.sock:/var/run/linbox.sock \
  postgres:17
```

### Layer 1: Kernel Primitives

- **PID namespace** — virtual PIDs
- **Network namespace** — isolated network stack with veth pair to LinBox bridge
- **Mount namespace** — overlay FS with FUSE mount for `/dev/urandom`
- **Time namespace** — CLOCK_MONOTONIC/BOOTTIME offsets (initial time set)
- **User namespace** — rootless operation

### Layer 2: Network & Resource Control

- **veth pair** connecting container to LinBox bridge — all inter-box traffic routed through controller
- **tc/netem** on the veth interface — configurable latency, loss, jitter, bandwidth, reordering
- **iptables rules** in the container's network namespace — partition simulation
- **cgroups v2** — CPU quota (simulate contention), memory limit, I/O bandwidth

```bash
# Example: add 50ms latency with 10ms jitter to Box A
tc qdisc add dev veth-a root netem delay 50ms 10ms distribution normal

# Example: simulate network partition between Box A and Box B
iptables -A OUTPUT -d 10.0.0.2 -j DROP
```

### Layer 3: Syscall Interception (The Core)

**liblinbox.so** — the LD_PRELOAD shim. This is the main interception mechanism.

**Communication with LinBox controller:**
- **Shared memory** (mmap'd region) for hot-path reads: virtual time, random seed state
- **Unix socket** for synchronous requests: capability negotiation, configuration updates
- **eventfd** for notifications: controller signals time changes

### Layer 4: Protocol-Level (IBI)

For inbound boundary interception (simulating how the service appears to clients):
- **TPROXY** for transparent proxying of wire protocols (Postgres wire, Redis RESP)
- **eBPF TC** for programmable packet-level interception
- Both work within network namespace

---

## 5. The liblinbox.so Shim — Design

### 5.1 Initialization

The shim's `__attribute__((constructor))` function runs before `main()`:

1. **Establish SBP connection** to LinBox controller via unix socket
2. **Negotiate capabilities**: which OBI hooks are active, fidelity levels
3. **Map shared memory** region for hot-path data (virtual time, PRNG state)
4. **Overwrite AT_RANDOM** 16 bytes with seeded value (deterministic stack canaries)
5. **Install seccomp-bpf filter** as safety net for direct syscalls
6. **Register SIGSYS handler** for trapped syscalls
7. **Disable rdrand/rdseed** via CPUID mask if available (Shadow approach)

### 5.2 Hot Path: Time

Time calls are the most frequent syscall in most applications. PostgreSQL calls `clock_gettime` thousands of times per query. The hot path must be **zero-IPC**:

```
LinBox controller → writes virtual time to shared memory
liblinbox.so     → reads from shared memory (single atomic read)
                    no IPC, no context switch, no signal
```

This is exactly how Shadow handles it. Overhead: a single atomic load vs a vDSO call — comparable performance.

### 5.3 Hot Path: Random

PRNG state stored in shared memory. The shim reads the current seed, generates bytes, advances the seed. No IPC needed.

```
getrandom(buf, len) → shim reads seed from shared memory
                    → chacha20(seed, counter) → fill buf
                    → advance counter
```

### 5.4 Network: Hybrid Approach

- **Most network operations**: pass through to real kernel networking within the network namespace. tc/netem adds latency and loss.
- **DNS resolution**: intercepted via LD_PRELOAD on `getaddrinfo` — return controlled addresses
- **External calls**: all egress goes through veth pair where LinBox controller can intercept, delay, or drop

### 5.5 Fork Handling

PostgreSQL forks per backend. Each fork inherits LD_PRELOAD and the seccomp filter, but needs its own state:

```c
pid_t fork_wrapper(void) {
    pid_t pid = real_fork();
    if (pid == 0) {
        // Child process
        reinit_linbox_state();  // new PRNG stream, register with controller
    }
    return pid;
}
```

The shim intercepts `fork()`, `clone()`, and `clone3()`. On `execve()`, the child gets a fresh shim initialization (LD_PRELOAD is inherited through environment).

### 5.6 Seccomp Fallback

For Go programs, static binaries, or any direct `syscall` instruction:

```c
void sigsys_handler(int sig, siginfo_t *info, void *ctx) {
    ucontext_t *uc = (ucontext_t *)ctx;
    long syscall_nr = uc->uc_mcontext.gregs[REG_RAX];

    switch (syscall_nr) {
    case __NR_clock_gettime:
        handle_clock_gettime(uc);
        return;
    case __NR_getrandom:
        handle_getrandom(uc);
        return;
    // ... other intercepted syscalls
    default:
        // Allow non-intercepted syscalls to proceed
        real_syscall(uc);
    }
}
```

---

## 6. Edge Cases and Bypasses

### 6.1 io_uring

**Problem:** io_uring submits operations via shared memory ring buffers. Individual operations (read, write, connect, accept, etc.) bypass both LD_PRELOAD and per-syscall seccomp filtering. seccomp sees `io_uring_enter` but NOT individual operations.

**Solution:** Block `io_uring_setup` (syscall 425), `io_uring_enter` (426), and `io_uring_register` (427) via seccomp. The application falls back to regular syscalls.

**Impact:** PostgreSQL 18 uses io_uring for async I/O — loses up to 3x throughput improvement. Acceptable for simulation because we're controlling time anyway. Most other services don't use io_uring yet. Docker already blocks io_uring by default.

### 6.2 vDSO

**Problem:** `clock_gettime`, `gettimeofday`, `time` execute in userspace via vDSO — no actual syscall, invisible to seccomp and ptrace.

**Solution:** LD_PRELOAD intercepts the libc wrapper **before** it calls the vDSO function. This covers 99.9%+ of cases. Direct vDSO calls (bypassing libc) are extremely rare.

**If needed:** Set kernel parameter `vdso=0` to force all time calls through real syscalls (4x slowdown for time calls). Or unmap the vDSO page in the shim constructor.

### 6.3 Go Runtime

**Problem:** Go makes syscalls directly via assembly (e.g., `runtime.futex`, `runtime.clock_gettime`), bypassing libc entirely. `CGO_ENABLED=1` only uses libc for CGO calls; the Go runtime itself still uses direct syscalls.

**Solution:** The seccomp-bpf safety net catches all direct syscalls. Go programs will hit the SIGSYS handler for intercepted syscalls (~800ns overhead per trapped call). For time-heavy Go code, this adds measurable but acceptable overhead.

**Alternative:** The `go2libc` project installs a seccomp filter that forces Go's direct syscalls through libc wrappers, enabling LD_PRELOAD interception.

### 6.4 Static Binaries

**Problem:** Statically linked binaries (musl/Alpine, Rust with musl target) don't use the dynamic linker. LD_PRELOAD is completely ignored.

**Solution:** The seccomp filter catches everything. All intercepted syscalls go through the SIGSYS handler. This is the same approach Shadow uses — if the shim can't intercept at the libc level, seccomp is the universal fallback.

**Detection:** `file /path/to/binary` or `ldd /path/to/binary` reveals static linking. The shim constructor can detect this and log a notice.

### 6.5 RDTSC / RDRAND Instructions

**Problem:** `RDTSC` reads the CPU timestamp counter directly — not a syscall. `RDRAND`/`RDSEED` generate hardware random numbers — not a syscall.

**Solution for RDRAND/RDSEED:** Shadow masks these via CPUID — tells the process they're unavailable, forcing fallback to `getrandom()`.

**Solution for RDTSC:** In a VM (Antithesis approach), the hypervisor traps RDTSC. In a container, RDTSC cannot be intercepted without ptrace. Most applications don't use RDTSC directly — they use `clock_gettime`. For the rare cases (high-frequency trading, benchmarking), accept the leakage or require VM-level sandboxing.

### 6.6 Summary: Coverage Matrix

| Bypass | LD_PRELOAD | seccomp TRAP | seccomp unotify | Net ns + tc | Block |
|---|---|---|---|---|---|
| libc calls | ✅ intercepts | ✅ fallback | ✅ fallback | — | — |
| Go direct syscalls | ❌ | ✅ intercepts | ✅ intercepts | — | — |
| Static binaries | ❌ | ✅ intercepts | ✅ intercepts | — | — |
| io_uring | ❌ | ❌ (can't see ops) | ❌ | — | ✅ block setup |
| vDSO time calls | ✅ (libc wrapper) | ❌ (no syscall) | ❌ | — | disable vDSO |
| RDTSC | ❌ | ❌ | ❌ | — | accept leakage |
| RDRAND/RDSEED | ❌ | ❌ | ❌ | — | CPUID mask |
| Network I/O | ✅ | ✅ | ✅ | ✅ | — |
| DPDK/RDMA | ❌ | ❌ | ❌ | ❌ | block device access |

**Effective coverage:** ~99.9% of real-world service workloads. The remaining 0.1% (RDTSC, kernel bypass networking) is either blockable or acceptable leakage for simulation purposes.

---

## 7. Prior Art — What Works in Production

### 7.1 Antithesis — Deterministic Hypervisor

- **Approach:** Custom bhyve fork with Intel VMX. Single vCPU per VM. All time sources virtualized (TSC, HPET). Deterministic PRNG for randomness.
- **Coverage:** 100% — everything goes through the hypervisor
- **Overhead:** Not disclosed, but practical for continuous testing
- **Pricing:** $2/hour per core, $20K-100K+/year enterprise
- **Relevance:** The "gold standard" — zero code changes, total determinism. But closed-source, expensive, and requires their cloud platform.

### 7.2 Shadow — LD_PRELOAD + seccomp Hybrid

- **Approach:** Exactly the dual-layer strategy recommended for LinuxBox. LD_PRELOAD shim for fast path, seccomp-bpf for safety net. Shared memory for time.
- **Coverage:** 164 syscalls reimplemented. Full network simulation.
- **Overhead:** Near-zero for hot path (shared memory time reads)
- **Relevance:** **Closest existing system to LinuxBox.** Open source, well-documented, award-winning (USENIX ATC '22 Best Paper).

### 7.3 gVisor — Application Kernel

- **Approach:** User-space kernel in Go. 274/350 syscalls reimplemented. Systrap (seccomp + SIGSYS) for interception.
- **Coverage:** Complete at syscall level — nothing leaks
- **Overhead:** ~800ns per syscall (11x native)
- **Relevance:** Proves that complete user-space syscall interception is viable at scale. Used in Google Cloud.

### 7.4 FoundationDB Sim2 — Interface Swap

- **Approach:** Compile-time interface polymorphism. `g_network` pointer swaps real/simulated implementations.
- **Coverage:** Complete for their interfaces (network, disk, time, random)
- **Relevance:** Proves the value of deterministic simulation. Trillion CPU-hour equivalent testing. But requires designing the system for it from day one — not applicable to existing services.

### 7.5 libfaketime — Time-Only LD_PRELOAD

- **Approach:** LD_PRELOAD shim that intercepts time functions only. Returns offset/scaled time.
- **Coverage:** Time only. No randomness, no network, no filesystem.
- **Relevance:** Proves LD_PRELOAD works for time interception in practice. Widely used. Starting point for liblinbox.so.

### 7.6 Comparison

| System | Coverage | Code Changes | Languages | Open Source | Overhead |
|---|---|---|---|---|---|
| **Antithesis** | 100% | None | All | No | Medium (VM) |
| **Shadow** | 164 syscalls | None | All (dynamic) | Yes | Low (shared mem) |
| **gVisor** | 274 syscalls | None | All | Yes | ~11x per syscall |
| **FDB Sim2** | Full (interfaces) | Must use Flow | C++ only | Yes | Near-zero |
| **libfaketime** | Time only | None | All (dynamic) | Yes | Near-zero |
| **LinuxBox (proposed)** | ~94 OBI points | None | All | — | Low-Medium |

---

## 8. Cost Estimation

### 8.1 Compute Costs (Running LinuxBox)

**Per-box overhead** (steady state):

| Component | CPU Overhead | Memory Overhead |
|---|---|---|
| Container + namespaces | ~0% | ~5-10 MB (kernel structures) |
| LD_PRELOAD shim | ~0% (shared memory reads) | ~1-2 MB (shim + shared region) |
| seccomp filter evaluation | <0.1% (bitmap cache) | Negligible |
| tc/netem | ~0% (kernel-level) | Negligible |
| LinBox controller process | ~1-5% per box | ~10-50 MB |
| FUSE (if used) | 2-4x latency on FS ops | ~10-20 MB |

**Total per-box:** ~1-5% CPU overhead, ~30-80 MB memory. For a typical 8-box simulation (PG + Redis + Node + Nginx + ...) on a 4-core 16GB machine: ~10-20% CPU overhead, ~500 MB memory for LinBox infrastructure.

### 8.2 Implementation Costs (Claude Code API tokens)

Estimating by component, based on complexity and lines of code:

| Component | Estimated LOC | Complexity | Estimated API Cost |
|---|---|---|---|
| **liblinbox.so (C)** — core shim with time, random, network, fork | ~3,000-5,000 | High (C, syscall ABI, shared memory) | $50-100 |
| **seccomp filter + SIGSYS handler** | ~500-1,000 | Medium (BPF programs, signal handling) | $20-40 |
| **LinBox controller** — SBP integration, shared memory management | ~2,000-3,000 | Medium (event loop, state management) | $30-60 |
| **Container setup** — Docker compose, namespace config, tc/netem | ~500-1,000 | Low (configuration, scripting) | $10-20 |
| **SBI definition for LinuxBox** — hook points, capability negotiation | ~1,000-2,000 | Medium (TypeScript, protocol) | $20-40 |
| **Tests** — determinism verification, fork handling, Go binary support | ~2,000-3,000 | Medium | $30-50 |
| **Integration** — connecting to existing LinBox Lab/Sim/Law infrastructure | ~1,000-2,000 | Medium | $20-40 |
| **TOTAL** | **~10,000-17,000** | | **$180-350** |

**Notes:**
- These are rough estimates for API costs using Claude for code generation
- Actual effort depends heavily on iteration cycles (debugging C code is harder than TypeScript)
- The hardest part is the C shim — syscall ABI, signal safety, reentrancy, fork handling
- The seccomp integration is the second hardest — BPF programs are hard to debug

### 8.3 Hosting Costs (Running Simulations)

| Scenario | Infrastructure | Monthly Cost |
|---|---|---|
| **Dev/testing** (single LinuxBox, 1 service) | 2-core, 4GB VM | $10-20/mo |
| **CI pipeline** (4-box simulation, on-demand) | 4-core, 16GB, spot instances | $50-100/mo |
| **Continuous simulation** (8-box, 24/7) | 8-core, 32GB dedicated | $200-400/mo |
| **Large-scale** (multiple labs, parallel) | 32-core, 128GB | $800-1600/mo |

Compare to Antithesis: $2/hr/core = ~$1,440/mo for a single core 24/7. LinuxBox is 10-100x cheaper because it doesn't need a hypervisor.

---

## 9. Implementation Roadmap

### Phase 1: Time + Random (MVP)

Minimum viable LinuxBox that controls time and randomness.

1. **liblinbox.so** — intercept `clock_gettime`, `gettimeofday`, `time`, `getrandom`. Shared memory for virtual time. Seeded PRNG for randomness.
2. **seccomp filter** — trap the same syscalls as safety net
3. **LinBox controller** — manage virtual time, accept SBP connections
4. **Docker wrapper** — container setup with LD_PRELOAD and seccomp profile
5. **Test:** Run PostgreSQL with virtual time. Verify `now()` returns controlled value. Verify `uuid_generate_v4()` is deterministic.

**Deliverable:** A container where PostgreSQL's `now()` returns whatever LinBox tells it. Deterministic random. Same query, same seed → same result.

### Phase 2: Network + Timers

6. **tc/netem integration** — configurable latency, loss between boxes
7. **Timer virtualization** — intercept `nanosleep`, `timerfd_*`, `alarm`. Virtual timer queue in the shim.
8. **DNS interception** — `getaddrinfo` returns controlled addresses
9. **Fork handling** — proper per-process state initialization
10. **Test:** Multi-box setup (PG + Node). Inject 200ms network latency between them. Verify Node's timeout logic works correctly with virtual time.

### Phase 3: Full OBI Coverage

11. **Filesystem timestamps** — `stat` returns virtual time for timestamps
12. **FUSE for /dev/urandom** — deterministic random device
13. **Process control** — proper `clone3` handling, `execve` re-injection
14. **Go binary support** — verify seccomp fallback works for Go services
15. **eBPF observability** — syscall monitoring, latency metrics

### Phase 4: Advanced (if needed)

16. **gVisor fork** — if LD_PRELOAD + seccomp proves insufficient for specific workloads
17. **io_uring interception** — if blocking is not acceptable
18. **Multi-architecture** — ARM64 support

---

## 10. Open Questions

### Resolved by this Research

1. ~~Can LD_PRELOAD cover all OBI points?~~ **No.** Need seccomp safety net for Go, static binaries, direct syscalls.
2. ~~How does `fork()` work with LD_PRELOAD?~~ Each fork inherits LD_PRELOAD. Shim must reinit per-process state.
3. ~~What about io_uring?~~ Block `io_uring_setup` via seccomp. Application falls back to regular I/O.
4. ~~How does shared memory time work?~~ Controller writes to mmap'd region, shim reads atomically. Zero IPC. Proven by Shadow.
5. ~~What's the performance overhead?~~ ~100ns per intercepted libc call, ~800ns per seccomp-trapped call, zero for kernel primitives.

### Still Open

6. **SBP communication protocol for LinuxBox** — exact message format between liblinbox.so and LinBox controller. Options: protobuf over unix socket, shared memory ring buffer, or custom binary format.
7. **Virtual timer queue design** — when `nanosleep(5s)` is called, the shim must: (a) register with controller, (b) block the calling thread, (c) wake when virtual time advances past the deadline. How to handle thousands of concurrent timers across multiple processes?
8. **Multi-process virtual time consistency** — PostgreSQL has multiple backends, each in a separate process. How to ensure all backends see consistent virtual time? Shared memory solves reads, but what about timer coordination?
9. **FUSE performance for database workloads** — PostgreSQL's WAL writes and page reads through FUSE may be too slow. Need benchmarks. May be better to use pass-through filesystem with LD_PRELOAD intercepting only timestamps.
10. **Hot restart / snapshot** — can we snapshot a LinuxBox (CRIU-style) and restore it later? Would enable Antithesis-style multiverse exploration.

---

## References

### Research Documents (this project)

- [LD_PRELOAD Interception](ld-preload.md) — syscall/libc interception via LD_PRELOAD
- [Seccomp-BPF and Ptrace](seccomp-ptrace.md) — deep dive on interception mechanisms
- [Deterministic Simulation Systems](deterministic-systems.md) — Antithesis, Shadow, FDB, rr, Hermit, MadSim, TigerBeetle
- [Alternative Sandbox Approaches](sandbox-alternatives.md) — gVisor, FUSE, namespaces, timens, eBPF, unikernels

### External Sources

- [Shadow Simulator Design](https://shadow.github.io/docs/guide/design_2x.html) — closest prior art
- [Shadow USENIX ATC '22 Paper](https://www.usenix.org/system/files/atc22-jansen.pdf) — LD_PRELOAD + seccomp hybrid
- [Antithesis Blog: Deterministic Hypervisor](https://antithesis.com/blog/deterministic_hypervisor/) — bhyve fork approach
- [gVisor Systrap](https://gvisor.dev/blog/2023/04/28/systrap-release/) — seccomp + SIGSYS interception at scale
- [libfaketime](https://github.com/wolfcw/libfaketime) — mature time-only LD_PRELOAD
- [seccomp(2) man page](https://man7.org/linux/man-pages/man2/seccomp.2.html) — kernel API reference
- [seccomp_unotify(2)](https://man7.org/linux/man-pages/man2/seccomp_unotify.2.html) — user notification API
- [time_namespaces(7)](https://man7.org/linux/man-pages/man7/time_namespaces.7.html) — Linux time namespace

---

[← Back](README.md)
