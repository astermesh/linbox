# Alternative Approaches to Building a Hookable Linux Sandbox

Research into mechanisms beyond LD_PRELOAD and ptrace for building a fully hookable, deterministic simulation sandbox for LinBox's LinuxBox concept.

## Context

LinBox proposes Docker containers with `LD_PRELOAD`-based interception for OBI (Outbound Box Interface) control. This document explores the full landscape of Linux sandbox mechanisms beyond the two already well-understood approaches (LD_PRELOAD and ptrace), evaluating each for LinBox's requirements: intercepting time, randomness, network, and filesystem at syscall boundaries with minimal overhead.

## Table of Contents

- [1. gVisor (runsc)](#1-gvisor-runsc)
- [2. FUSE (Filesystem in Userspace)](#2-fuse-filesystem-in-userspace)
- [3. Linux Namespaces for Isolation](#3-linux-namespaces-for-isolation)
- [4. Linux Time Namespace (timens)](#4-linux-time-namespace-timens)
- [5. Seccomp User Notification (SECCOMP_RET_USER_NOTIF)](#5-seccomp-user-notification)
- [6. Network Interception Approaches](#6-network-interception-approaches)
- [7. eBPF for Syscall Hooking](#7-ebpf-for-syscall-hooking)
- [8. Unikernel / Microkernel Approaches](#8-unikernel--microkernel-approaches)
- [Comparison Matrix](#comparison-matrix)
- [Recommendations for LinBox](#recommendations-for-linbox)

---

## 1. gVisor (runsc)

### How It Works

[gVisor](https://gvisor.dev/docs/) is Google's application kernel -- a user-space reimplementation of the Linux kernel interface written in Go. The key component is the **Sentry**, which intercepts all syscalls from the sandboxed application and handles them without passing them to the host kernel.

Architecture:

```
┌──────────────────────────────────────┐
│  Application (unmodified)            │
└──────────────┬───────────────────────┘
               │ syscall
┌──────────────▼───────────────────────┐
│  Platform (Systrap / KVM)            │
│  intercepts syscall, redirects to:   │
└──────────────┬───────────────────────┘
               │
┌──────────────▼───────────────────────┐
│  Sentry (application kernel in Go)   │
│  - reimplements 274/350 syscalls     │
│  - manages memory, signals, threads  │
│  - own network stack (netstack)      │
│  - own filesystem implementation     │
└──────────────┬───────────────────────┘
               │ limited host syscalls (53-68)
┌──────────────▼───────────────────────┐
│  Gofer (file access proxy, 9P)       │
└──────────────┬───────────────────────┘
               │
          Host Kernel
```

### Platform Modes

gVisor supports three platform modes for [syscall interception](https://gvisor.dev/docs/architecture_guide/platforms/):

| Platform | Mechanism | Performance | Requirements |
|----------|-----------|-------------|--------------|
| **Systrap** (default since mid-2023) | `SECCOMP_RET_TRAP` sends SIGSYS, shared memory communication with Sentry | Medium overhead (~800ns vs ~70ns native) | No special hardware |
| **KVM** | Hardware virtualization extensions, Sentry acts as guest OS + VMM | Lower syscall cost on bare metal, poor with nested virt | VT-x/AMD-V, not nested |
| **ptrace** (deprecated) | `PTRACE_SYSEMU` traps every syscall | Highest overhead | None (runs anywhere) |

The [Systrap platform](https://gvisor.dev/blog/2023/04/28/systrap-release/) also employs an optimization: when it detects the common `mov sysno, %eax; syscall` pattern (7 bytes), it replaces it with a `jmp *%gs:offset` to a trampoline, avoiding the signal overhead entirely for hot paths.

### Syscall Coverage

Of 350 Linux syscalls (amd64), [274 have full or partial implementation](https://gvisor.dev/docs/user_guide/compatibility/linux/amd64/). The 76 unsupported syscalls are mostly obscure (e.g., `kexec_load`, `personality`, various `*_mq_*` operations). Most applications work because language runtimes have fallback paths for unsupported syscalls.

### Performance

- Syscall interception overhead: ~800ns per syscall (vs ~70ns native) -- about 11x
- CPU-bound workloads: negligible impact (few syscalls)
- I/O-bound / syscall-heavy workloads: significant impact
- [Production at Ant Group](https://gvisor.dev/blog/2021/12/02/running-gvisor-in-production-at-scale-in-ant/): 70% of apps have <1% overhead, 25% have <3% overhead
- Network: gVisor has its own network stack (netstack), performance is improving but adds overhead
- Filesystem: routing through Gofer (9P) adds latency per operation

### Time Handling

gVisor implements its own `Clock` interface in [`pkg/sentry/kernel/time`](https://pkg.go.dev/gvisor.dev/gvisor/pkg/sentry/kernel/time). The `Clock` interface has a `Now()` method returning nanoseconds and a `WallTimeUntil()` method. A `RealtimeClockFromContext` provides real time from context. All time syscalls (`clock_gettime`, `gettimeofday`, `time`) go through this abstraction.

### Extensibility for LinBox

gVisor does **not** provide a pluggable way to customize syscall handling. To add custom hooks (e.g., virtual time injection), you must [modify the source code directly](https://groups.google.com/g/gvisor-users/c/r4xrbpP9-pQ). The entry point is `pkg/sentry/syscalls/linux/` where all syscall implementations live. The `Clock` interface in `pkg/sentry/kernel/time` could be wrapped to return virtual time.

There is community interest in hooking ([GitHub issue #407](https://github.com/google/gvisor/issues/407)), but no pluggable API has been built.

### Suitability for LinBox

| Aspect | Assessment |
|--------|-----------|
| **Time control** | Possible via fork -- modify Clock implementation |
| **Random control** | Possible -- intercept `getrandom` syscall implementation |
| **Network control** | Full -- has its own network stack (netstack) |
| **Filesystem control** | Full -- has its own VFS and Gofer |
| **Transparency** | High -- application runs unmodified |
| **Performance** | 11x syscall overhead; acceptable for many workloads |
| **Maintenance burden** | Very high -- forking a large Go codebase (100k+ LOC) |
| **Language support** | All languages (intercepts at syscall level) |

**Verdict**: A custom gVisor fork is the **most powerful** approach -- it gives complete syscall-level control with no leakage (unlike LD_PRELOAD, which misses direct syscalls and static binaries). However, the maintenance burden of forking a large, actively-developed Google project makes this a Phase 3+ option. The Sentry architecture is essentially what Antithesis does at hypervisor level, but at user-space level.

---

## 2. FUSE (Filesystem in Userspace)

### How It Works

[FUSE](https://www.kernel.org/doc/html/next/filesystems/fuse.html) allows implementing filesystems in user space. The kernel FUSE module (`fuse.ko`) intercepts VFS operations on a mounted filesystem and routes them to a user-space daemon via `/dev/fuse`.

```
Application → open("/sim/data/file") → VFS → fuse.ko → /dev/fuse → FUSE daemon
                                                                        │
                                                              handles operation
                                                              (can intercept, modify,
                                                               redirect, delay, fail)
                                                                        │
                                                              response → fuse.ko → VFS → Application
```

### What FUSE Can Intercept

Every filesystem operation: `open`, `read`, `write`, `readdir`, `stat`, `mkdir`, `unlink`, `rename`, `chmod`, `truncate`, `fsync`, `getxattr`, `lock`, etc. The FUSE daemon has complete control over responses.

### Performance Overhead

[Research from USENIX FAST '17](https://www.usenix.org/system/files/conference/fast17/fast17-vangoor.pdf) and [ACM ToS 2019](https://dl.acm.org/doi/10.1145/3310148) measured FUSE overhead:

| Workload | FUSE Throughput vs Ext4 | Notes |
|----------|------------------------|-------|
| Small files | 43% of native | High metadata overhead |
| Large files | 44% of native | Data copy overhead |
| Mixed | ~50% of native | Context switches dominate |
| Latency | Up to 4x native | Kernel-userspace round trips |
| CPU utilization | Up to 31% increase | Context switching cost |

Recent improvements:
- **[RFUSE (FAST '24)](https://www.usenix.org/system/files/fast24-cho.pdf)**: Ring buffer communication, reduces queue contention
- **Darrick Wong's iomap patches (2025)**: FUSE_IOMAP_BEGIN/END bypass user-space for data I/O, potentially approaching native speed
- **FUSE passthrough mode**: For operations that don't need interception, pass directly to underlying filesystem

### Can FUSE Intercept /dev/urandom Reads?

Not directly -- FUSE operates on mounted filesystems, not character devices. However, two workarounds exist:

1. **Mount a FUSE filesystem over /dev/urandom**: Create a FUSE filesystem that mounts at `/dev/urandom` inside the container's mount namespace. When the application reads from it, the FUSE daemon returns deterministic bytes from a seeded PRNG.
2. **Bind-mount a FUSE-backed regular file**: Create a FUSE-backed file that returns deterministic random bytes and bind-mount it over `/dev/urandom`.

Both require `mount` privileges in the container (achievable via user namespace or `--privileged`).

### FUSE + Docker

Running FUSE inside Docker [requires specific capabilities](https://github.com/docker/for-linux/issues/321):

```bash
docker run --cap-add SYS_ADMIN --device /dev/fuse myimage
```

Or with security option:
```bash
docker run --security-opt apparmor:unconfined --cap-add SYS_ADMIN --device /dev/fuse myimage
```

### Virtiofs

[Virtiofs](https://www.kernel.org/doc/html/latest/filesystems/virtiofs.html) uses the FUSE protocol but replaces the `/dev/fuse` interface with a virtio device for VM-to-host filesystem sharing. It bypasses the kernel-userspace round trip on the guest side, communicating directly with the host via virtqueue. Docker Desktop uses virtiofs for host filesystem sharing on macOS.

### Suitability for LinBox

| Aspect | Assessment |
|--------|-----------|
| **Filesystem interception** | Full -- every VFS operation |
| **Time control** | No -- FUSE doesn't intercept time syscalls |
| **Random control** | Partial -- can mount over /dev/urandom |
| **Network control** | No |
| **Performance** | 2-4x latency increase, 50%+ throughput loss |
| **Complexity** | Moderate -- FUSE daemons are well-understood |

**Verdict**: FUSE is a **complementary tool**, not a standalone sandbox. It excels at filesystem interception and could provide the filesystem layer of a composite sandbox (FUSE for filesystem + tc/netem for network + LD_PRELOAD for time). The performance overhead is significant for I/O-heavy workloads like databases but may be acceptable for testing scenarios. The `/dev/urandom` trick is clever but fragile.

---

## 3. Linux Namespaces for Isolation

### Overview

Linux namespaces provide the isolation primitives that containers are built on. Each namespace type isolates a different system resource:

| Namespace | Flag | Isolates | Kernel Version |
|-----------|------|----------|---------------|
| **PID** | `CLONE_NEWPID` | Process IDs | 3.8 |
| **Network** | `CLONE_NEWNET` | Network stack (interfaces, routing, iptables) | 2.6.29 |
| **Mount** | `CLONE_NEWNS` | Filesystem mount points | 2.4.19 |
| **UTS** | `CLONE_NEWUTS` | Hostname, domain name | 2.6.19 |
| **IPC** | `CLONE_NEWIPC` | SysV IPC, POSIX message queues | 2.6.19 |
| **User** | `CLONE_NEWUSER` | UIDs, GIDs, capabilities | 3.8 |
| **Cgroup** | `CLONE_NEWCGROUP` | Cgroup root hierarchy | 4.6 |
| **Time** | `CLONE_NEWTIME` | CLOCK_MONOTONIC, CLOCK_BOOTTIME offsets | 5.6 |

### PID Namespace

Processes inside a PID namespace see a separate PID numbering starting from 1. The first process in the namespace becomes PID 1 (init). `getpid()` returns the namespace-local PID. Processes can have different PIDs depending on the namespace from which they're observed.

LinBox relevance: Provides virtual PIDs without any interception overhead -- the kernel handles it natively.

### Network Namespace

Each network namespace has its own network interfaces, routing table, iptables rules, and socket table. Communication between namespaces uses:

- **veth pairs**: Virtual Ethernet pairs connecting two namespaces
- **bridges**: Software bridges for multi-namespace connectivity
- **macvlan/ipvlan**: Direct attachment to physical interfaces

```
┌─ Namespace A ──┐         ┌─ Namespace B ──┐
│  eth0 (veth-a) │◄─veth─►│  eth0 (veth-b) │
│  10.0.0.1      │         │  10.0.0.2      │
└────────────────┘         └────────────────┘
```

LinBox relevance: Full control over network topology. All traffic between boxes goes through veth pairs, enabling inspection, modification, delay injection, and failure simulation at the network level.

### Mount Namespace

Each mount namespace has its own mount table. Changes to mounts (mount, umount, bind-mount) in one namespace are invisible to others.

LinBox relevance: Each box gets its own filesystem view. Can mount FUSE filesystems, overlay filesystems, or bind-mount modified files (e.g., a deterministic `/dev/urandom`) without affecting other boxes or the host.

### User Namespace

Maps UIDs/GIDs between the namespace and the host. A process can be root (UID 0) inside the namespace while being an unprivileged user on the host. This enables capabilities like `CAP_SYS_ADMIN` inside the namespace without actual host privileges.

LinBox relevance: Enables running services as root inside the box (as they expect in production) while maintaining host security. Also enables FUSE mounts and other privileged operations inside unprivileged containers.

### Cgroups for Resource Control

Cgroups (v2) control resource allocation:

| Controller | Controls | LinBox Use |
|-----------|----------|------------|
| **cpu** | CPU time, shares, quota | Simulate CPU contention |
| **memory** | Memory limit, swap | Simulate memory pressure |
| **io** | Block I/O bandwidth, IOPS | Simulate disk throttling |
| **pids** | Max number of processes | Simulate fork bomb protection |
| **cpuset** | CPU/memory node pinning | Simulate NUMA effects |

Cgroup controls are **dynamic** -- they can be adjusted at runtime without restarting the container, making them suitable for simulation scenarios where resource constraints change over time.

### Combining Namespaces

A complete isolation stack for a LinBox LinuxBox:

```
unshare --pid --net --mount --uts --ipc --user --time \
  --map-root-user /path/to/init
```

Docker/Podman handle this automatically. The key insight is that namespaces provide **kernel-level isolation** with **zero overhead** for the isolated resources -- unlike LD_PRELOAD or ptrace, there's no per-operation interception cost.

### Suitability for LinBox

| Aspect | Assessment |
|--------|-----------|
| **Isolation** | Complete -- each box is fully isolated |
| **PID control** | Built-in via PID namespace |
| **Network control** | Full -- via network namespace + veth |
| **Filesystem control** | Full -- via mount namespace |
| **Time control** | Partial -- only MONOTONIC/BOOTTIME offsets via timens |
| **Performance** | Near-zero overhead for isolation itself |
| **Dynamic control** | Yes -- cgroups adjustable at runtime |

**Verdict**: Namespaces are the **foundation layer** -- every LinuxBox approach should use them. They provide isolation and some control primitives (PID, network topology, filesystem view) essentially for free. They're necessary but not sufficient; additional mechanisms are needed for fine-grained interception (time, random, syscall-level hooks).

---

## 4. Linux Time Namespace (timens)

### How It Works

Available since Linux 5.6, the [time namespace](https://man7.org/linux/man-pages/man7/time_namespaces.7.html) allows per-namespace offsets to monotonic and boot-time clocks. Created via `unshare(CLONE_NEWTIME)` or `clone3()` with `CLONE_NEWTIME`.

### Usage

```bash
# Create time namespace with 1 day offset on CLOCK_MONOTONIC
unshare --time --monotonic 86400 -- bash

# Or programmatically:
# 1. unshare(CLONE_NEWTIME) -- creates ns, caller stays in old ns
# 2. Write offsets to /proc/self/timens_offsets:
#    "monotonic 86400 0"   -- 86400 seconds offset
#    "boottime  86400 0"   -- 86400 seconds offset
# 3. First child process enters the new namespace
```

### Supported vs Unsupported Clocks

| Clock | Virtualized? | Notes |
|-------|-------------|-------|
| `CLOCK_MONOTONIC` | Yes (offset) | Includes `_COARSE` and `_RAW` variants |
| `CLOCK_BOOTTIME` | Yes (offset) | Includes `_ALARM` variant |
| **`CLOCK_REALTIME`** | **No** | Deliberately excluded -- complexity and overhead in kernel |
| `CLOCK_PROCESS_CPUTIME_ID` | No | Per-process CPU time |
| `CLOCK_THREAD_CPUTIME_ID` | No | Per-thread CPU time |

### Critical Limitations

1. **No CLOCK_REALTIME**: The most commonly used clock for wall-clock time (`gettimeofday`, `time()`, `Date.now()`) is not virtualized. This is a deliberate kernel design decision to avoid complexity.

2. **Offsets only, not speed control**: You can shift the clock forward/backward by a fixed amount, but you cannot make time pass faster or slower. No speed multiplier.

3. **Write-once before first process**: Offsets can only be set before the first process enters the namespace. Once a process is inside, offsets are locked. Cannot be adjusted dynamically.

4. **Offset limits**: Cannot make the clock go negative; maximum ~146 years.

5. **OCI/runc support**: runc >= 1.2.0 supports time namespaces via `linux.timeOffsets` in the OCI spec. Docker supports it via runc.

### CLOCK_REALTIME Virtualization Alternatives

Since timens doesn't cover CLOCK_REALTIME, other mechanisms are needed:

| Mechanism | Can Virtualize CLOCK_REALTIME? | Notes |
|-----------|-------------------------------|-------|
| LD_PRELOAD (libfaketime) | Yes | Intercepts libc calls, misses direct syscalls |
| gVisor (Sentry) | Yes | Full reimplementation |
| ptrace | Yes | High overhead |
| seccomp + user notification | Yes | See section 5 |
| Hypervisor (Antithesis) | Yes | RDTSC virtualization |
| `clock_settime()` (Jepsen) | Yes | Actually changes system clock -- destructive |

### Suitability for LinBox

| Aspect | Assessment |
|--------|-----------|
| **CLOCK_MONOTONIC offset** | Built-in, zero overhead |
| **CLOCK_BOOTTIME offset** | Built-in, zero overhead |
| **CLOCK_REALTIME** | Not supported -- need supplementary mechanism |
| **Speed control** | Not supported -- offset only |
| **Dynamic adjustment** | Not supported -- write-once |
| **Performance** | Zero overhead (kernel-native) |

**Verdict**: Useful as a **free zero-overhead building block** for CLOCK_MONOTONIC/BOOTTIME offsets, but insufficient on its own. Must be combined with LD_PRELOAD or seccomp notifier for CLOCK_REALTIME and speed control. A good complement but not a replacement.

---

## 5. Seccomp User Notification

### How It Works

[`SECCOMP_RET_USER_NOTIF`](https://man7.org/linux/man-pages/man2/seccomp.2.html) (Linux 5.0+) is a seccomp action that, instead of killing the process or trapping with a signal, **pauses the syscall and notifies a supervisor process** via a file descriptor. The supervisor can then inspect the syscall, decide what to do, and send a response (including a faked return value).

```
Application                    Supervisor (LinBox controller)
    │                                │
    ├── syscall(clock_gettime) ──►   │
    │   [blocked]                    │
    │                                ├── SECCOMP_IOCTL_NOTIF_RECV
    │                                │   (receives: syscall nr, args, pid)
    │                                │
    │                                ├── decides: return virtual time
    │                                │
    │                                ├── SECCOMP_IOCTL_NOTIF_SEND
    │                                │   (sends: return value, errno)
    │   [unblocked] ◄───────────────┤
    │   receives fake value          │
    ▼                                ▼
```

This was originally [designed for LXD](https://brauner.io/2020/07/23/seccomp-notify.html) to allow a container manager to handle specific syscalls on behalf of containers.

### Key Properties

- **Selective**: Install a seccomp filter that only traps specific syscalls; everything else passes through at native speed
- **External supervisor**: The handler runs in a separate process, not inside the sandbox
- **File descriptor based**: Can be polled, multiplexed, shared across threads
- **Return value injection**: Supervisor provides the return value -- application never knows the real syscall wasn't executed
- **No kernel modification needed**: Standard Linux kernel feature (5.0+)

### What It Can Intercept

Any syscall. The seccomp filter matches on syscall number and (limited) argument inspection. The supervisor receives `struct seccomp_data` containing register values.

For LinBox, the key syscalls to trap:

| Syscall | OBI Category | Notification Handling |
|---------|-------------|----------------------|
| `clock_gettime` | Time | Return virtual time |
| `gettimeofday` | Time | Return virtual time |
| `time` | Time | Return virtual time |
| `getrandom` | Random | Return seeded PRNG bytes |
| `getpid` | Process | Return virtual PID |

### Performance

Each notified syscall requires:
1. Context switch to supervisor (the notification)
2. Supervisor processes the notification
3. Context switch back to target (the response)

This is slower than LD_PRELOAD (which stays in-process) but faster than ptrace (which requires 4 context switches per syscall -- entry, inspect, exit, inspect). Estimated overhead: ~2-5 microseconds per notified syscall.

Crucially, non-notified syscalls run at **native speed** with zero overhead beyond the initial seccomp filter evaluation (~10-50ns).

### Security Limitation: TOCTOU

The seccomp notifier has a [known TOCTOU vulnerability](https://docs.kernel.org/userspace-api/seccomp_filter.html): pointer arguments in `seccomp_data` point to the target's memory, which the target can modify while the supervisor inspects them. This matters for security sandboxing but is **not a concern for LinBox** -- LinBox trusts the sandboxed application; it's simulating the environment, not enforcing security.

### Advantages Over LD_PRELOAD

| Aspect | LD_PRELOAD | Seccomp Notifier |
|--------|-----------|-----------------|
| Static binaries | Cannot intercept | Intercepts (syscall level) |
| Go programs | Cannot intercept (direct syscalls) | Intercepts |
| Direct syscall instruction | Cannot intercept | Intercepts |
| `io_uring` | Cannot intercept | Partial (can block io_uring setup) |
| Multi-language support | Only libc-using programs | All programs |
| Performance (intercepted) | In-process (~100ns) | Cross-process (~2-5us) |
| Performance (non-intercepted) | Zero | ~10-50ns (filter eval) |

### Suitability for LinBox

| Aspect | Assessment |
|--------|-----------|
| **Time control** | Full -- intercept all time syscalls |
| **Random control** | Full -- intercept getrandom |
| **PID control** | Full -- intercept getpid |
| **Network control** | Partial -- can intercept socket syscalls but complex |
| **Language support** | Universal -- works with Go, Rust, C, static binaries |
| **Performance** | ~2-5us per intercepted syscall; native for non-intercepted |
| **Complexity** | Moderate -- requires supervisor process |
| **Maturity** | Production-ready -- used by LXD, Podman |

**Verdict**: The seccomp notifier is the **strongest alternative to LD_PRELOAD** for LinBox. It covers LD_PRELOAD's biggest gaps (static binaries, Go programs, direct syscalls) while running at acceptable overhead for simulation. The main downside is cross-process communication cost (~20-50x slower than in-process LD_PRELOAD for intercepted calls). An ideal strategy might be: seccomp notifier as the **guaranteed fallback** that catches everything, with LD_PRELOAD as an **optional optimization** for dynamically-linked programs.

---

## 6. Network Interception Approaches

### Approach Overview

| Method | Layer | Overhead | Programmability | Dynamic |
|--------|-------|----------|-----------------|---------|
| **iptables/nftables** | L3-L4 | Very low | Rules-based | Yes |
| **tc/netem** | L2-L4 | Very low | Qdisc-based | Yes |
| **eBPF (TC/XDP)** | L2-L4 | Very low (~1ms) | Fully programmable | Yes |
| **veth pair + bridge** | L2 | Very low | Topology-based | Yes |
| **TPROXY** | L4 | Low | Proxy-based | Yes |
| **mitmproxy** | L7 | Medium-High | Full inspection | Yes |

### iptables/nftables in Network Namespace

Each network namespace has its own iptables/nftables rules. This enables per-box firewall rules:

```bash
# Inside the box's network namespace:
iptables -A OUTPUT -p tcp --dport 443 -j DROP  # Block HTTPS
iptables -A OUTPUT -p tcp --dport 5432 -j REJECT  # Reject Postgres
```

nftables is the modern replacement with better syntax and performance. Used by Docker and Kubernetes for service routing.

### tc/netem for Latency/Loss Injection

[tc netem](https://man7.org/linux/man-pages/man8/tc-netem.8.html) is the standard Linux tool for network condition emulation:

```bash
# Add 100ms delay with 20ms jitter (normal distribution)
tc qdisc add dev eth0 root netem delay 100ms 20ms distribution normal

# Add 0.1% packet loss
tc qdisc change dev eth0 root netem loss 0.1%

# Add 1% packet duplication
tc qdisc change dev eth0 root netem duplicate 1%

# Add packet reordering
tc qdisc change dev eth0 root netem delay 10ms reorder 25% 50%

# Combine with rate limiting (chain TBF after netem)
tc qdisc add dev eth0 root handle 1:0 netem delay 100ms
tc qdisc add dev eth0 parent 1:1 handle 10: tbf rate 256kbit buffer 1600 limit 3000
```

Key properties:
- **Outbound only**: netem applies to egress traffic. For bidirectional effects, apply on both sides of the veth pair.
- **Requires NET_ADMIN**: `docker run --cap-add NET_ADMIN`
- **Dynamic**: Rules can be changed at runtime without restarting the container
- **Selective**: Can apply to specific destination IPs/ports using `u32` filters and `prio` qdiscs

tc netem is already used by chaos engineering tools ([Pumba](https://github.com/alexei-led/pumba), Chaos Mesh, etc.) for container network emulation.

### eBPF for Network Interception

eBPF programs can attach to network interfaces at multiple points:

| Hook Point | eBPF Type | What It Can Do |
|-----------|----------|---------------|
| **XDP** | `BPF_PROG_TYPE_XDP` | Earliest hook, before sk_buff allocation. Drop, redirect, modify packets. |
| **TC ingress/egress** | `BPF_PROG_TYPE_SCHED_CLS` | After sk_buff creation. Full packet access, modify, redirect. |
| **cgroup/connect4** | `BPF_PROG_TYPE_CGROUP_SOCK_ADDR` | Intercept connect(), modify destination IP/port. |
| **sockops** | `BPF_PROG_TYPE_SOCK_OPS` | Connection lifecycle events (established, close, etc.) |
| **sk_msg** | `BPF_PROG_TYPE_SK_MSG` | Intercept sendmsg() at socket level. |

An [eBPF transparent proxy](https://github.com/dorkamotorka/transparent-proxy-ebpf) intercepts `connect()` via `cgroup/connect4`, redirects to a local proxy, and stores original destination in an eBPF map. The proxy retrieves the original destination via `getsockopt`. Overhead: ~1ms per connection, CPU impact <0.5%.

### veth Pair + Bridge for Traffic Inspection

```
┌─ Box A ─────┐    ┌─ Bridge ─────────┐    ┌─ Box B ─────┐
│  eth0       │    │                   │    │  eth0       │
│  (veth-a)  ├────┤  br0              ├────┤  (veth-b)  │
│             │    │   │               │    │             │
└─────────────┘    │   ▼               │    └─────────────┘
                   │  tc/eBPF attached │
                   │  (inspect/modify) │
                   └───────────────────┘
```

All traffic between boxes passes through the bridge, where tc/eBPF programs can inspect, modify, delay, or drop packets.

### TPROXY for Transparent Proxying

[TPROXY](https://docs.kernel.org/networking/tproxy.html) allows transparent proxying without NAT -- the proxy sees the original source and destination IPs:

```bash
# Route traffic to the local proxy
iptables -t mangle -A PREROUTING -p tcp --dport 5432 \
  -j TPROXY --on-port 9999 --tproxy-mark 0x1/0x1
ip rule add fwmark 0x1 lookup 100
ip route add local 0.0.0.0/0 dev lo table 100
```

The proxy at port 9999 receives the connection with original src/dst preserved. This is how service meshes (Istio, Linkerd) intercept traffic.

### Suitability for LinBox

| Approach | Best For | LinBox Use |
|----------|---------|------------|
| **iptables/nftables** | Block/allow rules | Simulate network partitions |
| **tc/netem** | Latency, loss, jitter | Simulate WAN conditions, disk I/O latency |
| **eBPF (TC)** | Programmable packet processing | Custom fault injection, protocol-aware manipulation |
| **veth + bridge** | Topology control | Inter-box network topology |
| **TPROXY** | Protocol-level inspection | Wire protocol interception (Postgres, Redis) |

**Verdict**: Network interception is a **solved problem** with mature, production-ready tools. The recommended stack for LinBox is: veth pairs for topology, tc/netem for condition simulation, iptables for partition simulation, and TPROXY or eBPF for protocol-level interception. All of these work within network namespaces and Docker containers with appropriate capabilities.

---

## 7. eBPF for Syscall Hooking

### Hook Mechanisms

eBPF provides several mechanisms for syscall-level observation and (limited) modification:

#### Kprobes / Kretprobes

Attach to any kernel function, including syscall entry points:

```c
SEC("kprobe/__x64_sys_clock_gettime")
int BPF_KPROBE(hook_clock_gettime, int clockid) {
    // Observe syscall arguments
    // Can read/store data but cannot modify return value
    return 0;
}
```

- **Overhead**: ~100-200ns per probe hit (exception-based mechanism)
- **Portability**: Tied to kernel version (function names may change)
- **Limitation**: Read-only observation -- cannot modify syscall arguments or return values

#### Fentry / Fexit (BPF Trampolines)

Newer, faster alternative to kprobes (Linux 5.5+):

```c
SEC("fentry/__x64_sys_clock_gettime")
int BPF_PROG(hook_clock_gettime, struct pt_regs *regs) {
    // Access arguments directly (no helper functions needed)
    return 0;
}

SEC("fexit/__x64_sys_clock_gettime")
int BPF_PROG(hook_clock_gettime_exit, struct pt_regs *regs, long ret) {
    // Access both arguments AND return value
    return 0;
}
```

- **Overhead**: Near-zero (~10-50ns) -- uses BPF trampolines instead of breakpoints
- **Portability**: Requires `CONFIG_FUNCTION_TRACER` and kernel 5.5+
- **Advantage over kprobes**: fexit sees both input parameters and return value

#### LSM (Linux Security Module) Hooks

eBPF programs can attach to LSM hooks for security policy enforcement:

```c
SEC("lsm/bprm_check_security")
int BPF_PROG(restrict_exec, struct linux_binprm *bprm) {
    // Can block execution
    return -EPERM;
}
```

### What eBPF Can and Cannot Do for LinBox

| Capability | Status | Notes |
|-----------|--------|-------|
| **Observe syscalls** | Yes | Full argument and return value visibility |
| **Modify syscall return values** | **No** | eBPF verifier prevents this |
| **Modify syscall arguments** | **No** | eBPF verifier prevents this |
| **Block syscalls** | Partial | LSM hooks can deny, tracepoints cannot |
| **Inject custom responses** | **No** | Not possible with standard eBPF |
| **Observe network traffic** | Yes | TC, XDP, sk_msg hooks |
| **Modify network traffic** | Yes | TC and XDP can rewrite packets |
| **Attach per-container** | Yes | Cgroup-scoped eBPF programs |

### Tetragon (Cilium) for Policy-Based Hooking

[Tetragon](https://tetragon.io/docs/concepts/tracing-policy/hooks/) provides a higher-level interface for eBPF-based syscall observation and enforcement:

```yaml
spec:
  kprobes:
    - call: "__x64_sys_clock_gettime"
      syscall: true
      args:
        - index: 0
          type: "int"
      returnArg:
        type: "int"
```

### Suitability for LinBox

| Aspect | Assessment |
|--------|-----------|
| **Observation** | Excellent -- full syscall visibility with minimal overhead |
| **Interception** | Not possible -- eBPF cannot modify syscall results |
| **Network modification** | Yes -- TC/XDP hooks can rewrite packets |
| **Performance** | Near-zero for observation; ~1ms for network modifications |

**Verdict**: eBPF is excellent for **observability** (monitoring syscalls, tracking behavior, measuring latency) but **cannot replace LD_PRELOAD or seccomp notifier for syscall interception**. eBPF's verifier deliberately prevents modification of syscall arguments and return values. Use eBPF for the observation/feedback layer alongside seccomp or LD_PRELOAD for the interception/control layer.

---

## 8. Unikernel / Microkernel Approaches

### Overview

Instead of running the target application on a full Linux kernel and intercepting at the boundary, run it on a minimal kernel where you control everything.

### Unikraft

[Unikraft](https://unikraft.org/docs/concepts/compatibility) is a modular micro-library OS:

- **160+ syscalls** implemented (enough for Redis, Nginx, SQLite, Python, Ruby, Go)
- **Boot time**: Sub-millisecond on QEMU/Solo5, <1ms on Firecracker
- **Image size**: <2MB for most applications
- **Performance**: [35% faster than OSv on Redis, 25% faster for Nginx](https://unikraft.org/docs/concepts/performance)
- **Architecture**: Fully modular -- OS primitives are composable libraries

For LinBox, the key advantage: you can **replace the time, random, and network libraries** entirely with simulation-aware implementations. No interception needed -- you *are* the kernel.

### OSv

- Runs unmodified Linux binaries via dynamic linker
- Supports Java, C/C++, Node.js, Ruby, Go
- Single address space (no user/kernel boundary)
- Originally by Cloudius Systems (now ScyllaDB), community-maintained
- [Security concerns](https://x41-dsec.de/news/missing-or-weak-mitigations-in-various-unikernels/): weak stack canary implementation, missing ASLR

### Nanos (NanoVMs)

- **Full Linux binary compatibility**: Runs any ELF binary without porting
- Implements Linux syscall interface
- Runs on KVM/QEMU, Xen, ESX, Hyper-V
- Cloud-ready (AWS, GCP)
- Best for "lift and shift" -- run existing Linux applications

### Key Tradeoffs

| Aspect | Standard Linux + Interception | Unikernel |
|--------|------------------------------|-----------|
| **Compatibility** | Full Linux compatibility | Varies: 160-300 syscalls |
| **Control** | Add hooks at boundaries | Control everything natively |
| **Performance** | Interception overhead | No interception overhead |
| **Multi-process** | Full support | Usually single-process |
| **Debugging** | Standard tools (gdb, strace) | Limited tooling |
| **Ecosystem** | All Linux tools work | Many tools missing |
| **Docker integration** | Native | Requires separate runtime |
| **Concurrency** | Full multi-threading | Often limited |

### Critical Limitation: Multi-Process

Most unikernels are single-address-space, single-process systems. PostgreSQL, Redis (with I/O threads), and many other services **fork** worker processes. This is fundamentally incompatible with most unikernel designs. Nanos has the best compatibility here (running unmodified ELF binaries), but even Nanos doesn't fully support `fork()`.

### Suitability for LinBox

| Aspect | Assessment |
|--------|-----------|
| **Time control** | Full (you implement the clock) |
| **Random control** | Full (you implement the PRNG) |
| **Network control** | Full (you implement the network stack) |
| **Compatibility** | Low-Medium -- many real services won't run |
| **Multi-process** | Poor -- most unikernels don't support fork() |
| **Debugging** | Limited |
| **Maintenance** | Very high -- maintaining a custom OS |

**Verdict**: Unikernels are theoretically ideal (total control, zero interception overhead) but practically unsuitable for LinBox's use case. The requirement to run **real, unmodified services** (PostgreSQL, Redis, Nginx) means we need Linux compatibility, including `fork()`, multi-threading, and the full syscall surface. Unikernels sacrifice exactly these things for performance and simplicity. This approach is better suited for purpose-built services, not for sandboxing arbitrary software.

---

## Comparison Matrix

| Mechanism | Time | Random | Network | Filesystem | Performance | Language Support | Complexity | Maturity |
|-----------|------|--------|---------|------------|-------------|-----------------|------------|----------|
| **LD_PRELOAD** | Yes | Yes | Partial | Yes | ~100ns/call | libc-using only | Low | High |
| **ptrace** | Yes | Yes | Yes | Yes | ~10us/call | Universal | Medium | High |
| **gVisor fork** | Yes | Yes | Yes | Yes | ~800ns/call | Universal | Very High | High |
| **Seccomp notifier** | Yes | Yes | Partial | Partial | ~2-5us/call | Universal | Medium | Medium-High |
| **FUSE** | No | Partial | No | Yes | 2-4x latency | N/A (filesystem) | Medium | High |
| **Namespaces** | Partial | No | Topology | View | Zero | N/A | Low | High |
| **timens** | MONO only | No | No | No | Zero | N/A | Low | High |
| **tc/netem** | No | No | Yes | No | Very Low | N/A (network) | Low | High |
| **eBPF** | Observe only | Observe only | Modify | Observe only | ~10-50ns | N/A | Medium-High | High |
| **Unikernel** | Full | Full | Full | Full | Zero overhead | Varies | Very High | Low-Medium |

---

## Recommendations for LinBox

### Layered Architecture

No single mechanism covers all requirements. The recommended approach is a **layered stack** where each mechanism handles what it does best:

```
┌───────────────────────────────────────────────────────────┐
│  Layer 4: Protocol-Level (IBI)                            │
│  TPROXY / eBPF transparent proxy for wire protocol        │
│  interception (Postgres, Redis, HTTP)                     │
├───────────────────────────────────────────────────────────┤
│  Layer 3: Syscall Interception (OBI)                      │
│  Primary: LD_PRELOAD (liblinbox.so)                       │
│  Fallback: seccomp SECCOMP_RET_USER_NOTIF                 │
│  (catches static binaries, Go, direct syscalls)           │
├───────────────────────────────────────────────────────────┤
│  Layer 2: Network & Resource Control                      │
│  veth pairs + tc/netem (latency, loss, bandwidth)         │
│  cgroups (CPU, memory, I/O throttling)                    │
│  iptables/nftables (partitions, firewall rules)           │
├───────────────────────────────────────────────────────────┤
│  Layer 1: Isolation (Kernel Primitives)                   │
│  PID namespace (virtual PIDs)                             │
│  Network namespace (isolated network stack)               │
│  Mount namespace (isolated filesystem)                    │
│  timens (CLOCK_MONOTONIC/BOOTTIME offsets)                │
│  User namespace (rootless operation)                      │
├───────────────────────────────────────────────────────────┤
│  Layer 0: Container Runtime                               │
│  Docker / Podman / containerd                             │
└───────────────────────────────────────────────────────────┘
```

### Phase 1 (Practical, Now)

1. **LD_PRELOAD** (`liblinbox.so`) for time, random, and selected syscall interception
2. **Namespaces** (PID, network, mount) via Docker for isolation
3. **tc/netem** for network condition simulation
4. **cgroups** for resource control
5. **timens** for CLOCK_MONOTONIC/BOOTTIME offsets (free, zero overhead)

### Phase 2 (Enhanced Control)

6. **Seccomp notifier** (`SECCOMP_RET_USER_NOTIF`) as fallback for programs that bypass LD_PRELOAD (Go, static binaries)
7. **FUSE** for filesystem-level interception where needed (e.g., deterministic `/dev/urandom`, I/O fault injection)
8. **eBPF** for observability (syscall monitoring, performance metrics, feedback loop)
9. **TPROXY** or eBPF transparent proxy for wire protocol inspection

### Phase 3 (Maximum Control, if needed)

10. **gVisor fork** with custom Sentry modifications for complete syscall-level control -- only if LD_PRELOAD + seccomp notifier prove insufficient

### The LD_PRELOAD + Seccomp Notifier Combo

The most promising finding from this research is the **dual-layer interception** strategy:

```
Application
    │
    ├── libc call (clock_gettime)
    │   └── intercepted by LD_PRELOAD → fast, in-process (~100ns)
    │
    ├── direct syscall (syscall instruction)
    │   └── intercepted by seccomp notifier → cross-process (~2-5us)
    │
    ├── Go runtime (direct syscall)
    │   └── intercepted by seccomp notifier → cross-process (~2-5us)
    │
    └── other syscalls (open, write, etc.)
        └── pass through at native speed
```

This gives universal coverage: LD_PRELOAD handles the common case fast, seccomp notifier catches everything that LD_PRELOAD misses. The performance cost of the seccomp path is acceptable because the syscalls that bypass libc (Go programs, static binaries) are the minority case.

---

## References

### gVisor
- [gVisor Documentation](https://gvisor.dev/docs/)
- [Platform Guide](https://gvisor.dev/docs/architecture_guide/platforms/)
- [Systrap Release Blog](https://gvisor.dev/blog/2023/04/28/systrap-release/)
- [gVisor at Ant Group](https://gvisor.dev/blog/2021/12/02/running-gvisor-in-production-at-scale-in-ant/)
- [Linux amd64 Syscall Compatibility](https://gvisor.dev/docs/user_guide/compatibility/linux/amd64/)
- [GitHub Issue #407 — Custom Syscall Hooks](https://github.com/google/gvisor/issues/407)
- [gVisor Sentry Time Package](https://pkg.go.dev/gvisor.dev/gvisor/pkg/sentry/kernel/time)

### FUSE
- [FUSE Kernel Documentation](https://www.kernel.org/doc/html/next/filesystems/fuse.html)
- [To FUSE or Not to FUSE (USENIX FAST '17)](https://www.usenix.org/system/files/conference/fast17/fast17-vangoor.pdf)
- [RFUSE: Modernizing FUSE (USENIX FAST '24)](https://www.usenix.org/system/files/fast24-cho.pdf)
- [FUSE Performance Study (ACM ToS)](https://dl.acm.org/doi/10.1145/3310148)
- [Virtiofs Kernel Documentation](https://www.kernel.org/doc/html/latest/filesystems/virtiofs.html)
- [Toward Fast Containerized User-Space Filesystems (LWN)](https://lwn.net/Articles/1044432/)

### Linux Namespaces & timens
- [time_namespaces(7) Man Page](https://man7.org/linux/man-pages/man7/time_namespaces.7.html)
- [Experiment with Time Namespace (Uchio Kondo)](https://udzura.medium.com/experiment-with-time-namespace-8be9ef435c05)

### Seccomp Notifier
- [Seccomp BPF Kernel Documentation](https://docs.kernel.org/userspace-api/seccomp_filter.html)
- [The Seccomp Notifier (Christian Brauner)](https://brauner.io/2020/07/23/seccomp-notify.html)
- [seccomp(2) Man Page](https://man7.org/linux/man-pages/man2/seccomp.2.html)
- [Seccomp-BPF: Confining Linux Processes](https://rahalkar.dev/posts/2026-02-23-seccomp-bpf-syscall-sandboxing/)

### Network Interception
- [tc-netem(8) Man Page](https://man7.org/linux/man-pages/man8/tc-netem.8.html)
- [Transparent Proxy with eBPF](https://github.com/dorkamotorka/transparent-proxy-ebpf)
- [dae: eBPF-Based Transparent Proxy](https://github.com/daeuniverse/dae)
- [Pumba: Container Chaos Testing](https://github.com/alexei-led/pumba)
- [Simulating Network Latency in Containers (Red Hat)](https://developers.redhat.com/articles/2025/05/26/how-simulate-network-latency-local-containers)

### eBPF Hooking
- [eBPF Tracepoints, Kprobes, or Fprobes (iximiuz)](https://labs.iximiuz.com/tutorials/ebpf-tracing-46a570d1)
- [Fentry and Fexit Guide](https://ebpf.hamza-megahed.com/docs/chapter3/5-fentry-fexit/)
- [Tetragon Hook Points](https://tetragon.io/docs/concepts/tracing-policy/hooks/)
- [Hunting Rootkits with eBPF (Aqua Security)](https://www.aquasec.com/blog/linux-syscall-hooking-using-tracee/)

### Unikernels
- [Unikraft Compatibility](https://unikraft.org/docs/concepts/compatibility)
- [Unikraft Performance](https://unikraft.org/docs/concepts/performance)
- [Unikernels Introduction (iximiuz)](https://labs.iximiuz.com/tutorials/unikernels-intro-93976514)
- [Missing Mitigations in Unikernels (X41)](https://x41-dsec.de/news/missing-or-weak-mitigations-in-various-unikernels/)
- [Are Unikernels Ready for Serverless on the Edge?](https://arxiv.org/html/2403.00515v1)

---

[← Back](README.md)
