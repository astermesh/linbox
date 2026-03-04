# Seccomp-BPF and Ptrace for Syscall Interception

Deep research into Linux kernel mechanisms for intercepting, filtering, and modifying system calls. Focus on building a deterministic simulation sandbox where all external interactions must be interceptable.

## Table of Contents

- [1. Seccomp-BPF](#1-seccomp-bpf)
- [2. Ptrace](#2-ptrace)
- [3. LD_PRELOAD + Seccomp-BPF Hybrid](#3-ld_preload--seccomp-bpf-hybrid)
- [4. seccomp_unotify (User Notification)](#4-seccomp_unotify-user-notification)
- [5. Real-World Implementations](#5-real-world-implementations)
- [6. Performance Comparison](#6-performance-comparison)
- [7. Implications for LinBox](#7-implications-for-linbox)

---

## 1. Seccomp-BPF

### 1.1 Overview

Seccomp (Secure Computing Mode) restricts the system calls available to a process. Added to Linux 2.6.12 (2005). Seccomp-BPF (Linux 3.5, 2012) generalized this by attaching a cBPF program that inspects syscall number and arguments, returning a verdict.

Key architectural point: seccomp-BPF runs **between the syscall entry and the actual handler dispatch**. If the filter says no, the kernel handler never executes.

### 1.2 Modes

**Strict mode** (`SECCOMP_MODE_STRICT`):
- Only 4 syscalls allowed: `read`, `write`, `_exit`, `sigreturn`
- Any other syscall kills the process
- Useful for pure computation sandboxes, but too restrictive for real applications

**Filter mode** (`SECCOMP_MODE_FILTER`):
- Attaches a cBPF program that inspects `struct seccomp_data`
- Returns a verdict per syscall
- Can filter on syscall number and raw integer arguments
- **Cannot dereference pointers** (deliberate security boundary)

### 1.3 The cBPF Virtual Machine

Seccomp uses **classic BPF (cBPF)**, not eBPF. This is deliberate: cBPF is simpler, has a smaller attack surface, and the kernel can verify it in bounded time.

The cBPF VM has:
- **Accumulator (A)** — 32-bit register for computation
- **Index register (X)** — 32-bit register for indirect addressing
- **Scratch memory** — 16 slots of 32-bit words (M[0]..M[15])
- **Read-only input** — `struct seccomp_data` describing the syscall

```c
struct seccomp_data {
    int   nr;                    /* syscall number */
    __u32 arch;                  /* AUDIT_ARCH_* value */
    __u64 instruction_pointer;   /* CPU IP */
    __u64 args[6];               /* syscall arguments */
};
```

Constraints:
- Maximum 4,096 instructions per filter
- Jumps can only go forward (guarantees halting)
- Only 32-bit word operations (BPF_W) — no BPF_H or BPF_B
- Cannot access process memory — only the `seccomp_data` struct

### 1.4 Filter Return Values (Verdicts)

In decreasing order of precedence:

| Verdict | Effect |
|---------|--------|
| `SECCOMP_RET_KILL_PROCESS` | Kill the entire process |
| `SECCOMP_RET_KILL_THREAD` | Kill the calling thread |
| `SECCOMP_RET_TRAP` | Send SIGSYS to the thread (does NOT execute syscall) |
| `SECCOMP_RET_ERRNO` | Return an errno to userspace (does NOT execute syscall) |
| `SECCOMP_RET_USER_NOTIF` | Forward to userspace supervisor (since Linux 5.0) |
| `SECCOMP_RET_TRACE` | Notify ptrace tracer (`PTRACE_EVENT_SECCOMP`) |
| `SECCOMP_RET_LOG` | Allow but log the syscall |
| `SECCOMP_RET_ALLOW` | Allow the syscall to execute normally |

### 1.5 SECCOMP_RET_TRACE — Trap to Ptrace Tracer

When the filter returns `SECCOMP_RET_TRACE`:
1. The kernel notifies the ptrace tracer of a `PTRACE_EVENT_SECCOMP` event
2. The lower 16 bits of the return value are available via `PTRACE_GETEVENTMSG`
3. The tracer can **skip** the syscall by changing the syscall number to -1
4. The tracer can **replace** the syscall by changing the syscall number
5. The tracer can **modify arguments** via `PTRACE_SETREGS`
6. If no tracer is attached, the process is killed with ENOSYS

This reduces ptrace overhead from 4 to 2 context switches per intercepted syscall, because seccomp-bpf can pre-filter and only notify the tracer for specific syscalls.

**Critical security note**: Sandboxes MUST NOT allow use of ptrace (even of other sandboxed processes) without extreme care, as ptracers can use this mechanism to escape the sandbox.

### 1.6 SECCOMP_RET_USER_NOTIF — User Notification

See [Section 4](#4-seccomp_unotify-user-notification) for detailed coverage.

### 1.7 Modifying Syscall Arguments and Return Values

With seccomp-bpf alone, you **cannot** modify arguments or return values. The filter is a read-only observer that returns a verdict. To modify:

| Mechanism | Can modify args? | Can modify return? | How |
|-----------|:---:|:---:|-----|
| `SECCOMP_RET_ERRNO` | No | Yes (errno only) | Lower 16 bits of verdict become errno |
| `SECCOMP_RET_TRACE` + ptrace | Yes | Yes | Tracer uses `PTRACE_SETREGS` at entry/exit |
| `SECCOMP_RET_USER_NOTIF` | No (see TOCTOU) | Yes | Supervisor uses `SECCOMP_IOCTL_NOTIF_SEND` to spoof return |
| `SECCOMP_RET_TRAP` + SIGSYS | Partial | Partial | Signal handler can modify, but limited |

### 1.8 Performance Characteristics

**Filter evaluation overhead**:
- Older kernels (linear search): ~1-6 microseconds per syscall
- Linux 5.11+ (constant-action bitmap cache): **tens of nanoseconds** for cached syscalls
- The kernel runs a BPF emulator at filter install time, identifies syscalls where the filter doesn't inspect `args` or `instruction_pointer` and always returns "allow", then caches this in a per-syscall-number bitfield
- Cached syscalls skip BPF evaluation entirely

**Filter stacking**: Multiple filters can be attached (each `fork`/`clone` inherits them). All filters run; the highest-precedence verdict wins. This can compound overhead.

### 1.9 Limitations

1. **Cannot dereference pointers** — only sees raw integer values in `seccomp_data.args[]`. Cannot inspect filenames, buffer contents, etc.
2. **Cannot modify anything** — pure read-only filter; needs ptrace or unotify to modify
3. **Irreversible** — once applied, filters cannot be removed (only made more restrictive)
4. **No eBPF** — limited to cBPF instruction set; no maps, no helper functions, no tail calls
5. **Architecture-dependent syscall numbers** — must verify `seccomp_data.arch` before filtering on `nr`
6. **32-bit argument inspection** — `args[]` are 64-bit but BPF loads are 32-bit, requiring two loads for full 64-bit values
7. **Thread inheritance** — `SECCOMP_FILTER_FLAG_TSYNC` syncs to all threads, but there's a race window
8. **io_uring bypass** — io_uring submissions can bypass seccomp filters (fixed in Linux 5.18+ with `IORING_SETUP_ENFORCE_SQTHREAD_CREDENTIALS`)

---

## 2. Ptrace

### 2.1 PTRACE_SYSCALL — Stop at Every Syscall

`PTRACE_SYSCALL` resumes the tracee and arranges for it to stop at the next entry to or exit from a system call.

Flow per syscall:
```
Tracee: enters syscall → STOP (entry)
  Tracer: PTRACE_GETREGS → inspect args
  Tracer: optionally PTRACE_SETREGS → modify args
  Tracer: PTRACE_SYSCALL → resume
Tracee: kernel executes syscall → STOP (exit)
  Tracer: PTRACE_GETREGS → inspect return value
  Tracer: optionally PTRACE_SETREGS → modify return
  Tracer: PTRACE_SYSCALL → resume
Tracee: continues
```

This produces **4 context switches per syscall** (tracee→tracer at entry, tracer→tracee to resume, tracee→tracer at exit, tracer→tracee to resume).

Entry and exit stops are **indistinguishable** — the tracer must track state. Use `PTRACE_O_TRACESYSGOOD` to set bit 7 in the signal number for syscall stops (distinguishes from signal-delivery stops).

### 2.2 Reading and Modifying Registers

**x86-64 register mapping for syscalls:**

| Register | Purpose |
|----------|---------|
| `orig_rax` | Syscall number (preserved; use this, not `rax`) |
| `rax` | Return value (after syscall exit) |
| `rdi` | Argument 1 |
| `rsi` | Argument 2 |
| `rdx` | Argument 3 |
| `r10` | Argument 4 |
| `r8` | Argument 5 |
| `r9` | Argument 6 |

**Why `orig_rax` vs `rax`**: The kernel uses `rax` for the return value but saves the original syscall number in `orig_rax`. At syscall entry, `rax` equals `orig_rax`. At syscall exit, `rax` contains the return value.

```c
// Read registers
struct user_regs_struct regs;
ptrace(PTRACE_GETREGS, pid, NULL, &regs);

// Modify syscall number (skip by setting to -1)
regs.orig_rax = -1;
ptrace(PTRACE_SETREGS, pid, NULL, &regs);

// Modify return value (at exit)
regs.rax = -EPERM;
ptrace(PTRACE_SETREGS, pid, NULL, &regs);
```

**Reading/writing process memory** (for pointer arguments like filenames):
- `PTRACE_PEEKTEXT` / `PTRACE_POKETEXT` — read/write one word at a time
- `process_vm_readv` / `process_vm_writev` — batch read/write (more efficient)
- On newer kernels: `PTRACE_GETREGSET` (since Linux 2.6.34) is more portable

### 2.3 Modifying String Arguments (e.g., File Paths)

Approach: save original string via `PTRACE_PEEKTEXT`, overwrite with new string via `PTRACE_POKETEXT`, then restore after syscall exit.

**Problems**:
- New string may be longer than original — risk of overwriting other data
- Other threads may be reading the same memory concurrently
- String may be in a read-only segment
- Not safe for multi-threaded processes

Safer approach: allocate new memory in the tracee (via `mmap` injected through ptrace), write string there, and point the argument to the new buffer.

### 2.4 Performance

| Metric | Value |
|--------|-------|
| Stops per syscall | 2 (entry + exit) |
| Context switches per stop | ~2 (tracer ↔ tracee) |
| Total context switches per syscall | ~4 |
| Cost per context switch | ~1.2-2.2 us |
| **Estimated overhead per traced syscall** | **~5-9 us** |
| Measured slowdown (getpid loop, 10M calls) | ~15x |
| Worst-case slowdown (syscall-heavy workloads) | 10-100x |

With `PTRACE_SYSEMU` (used by UML and gVisor's old ptrace platform): replaces entry+exit with a single stop, reducing to ~2 context switches per syscall. But still significantly slower than seccomp-based approaches.

### 2.5 Multi-Threaded Process Tracing

- Each thread must be individually attached to the tracer
- `PTRACE_O_TRACECLONE` / `PTRACE_O_TRACEFORK` / `PTRACE_O_TRACEVFORK` — auto-trace new threads/processes
- The tracer receives `PTRACE_EVENT_CLONE` etc. when new threads are created
- Each thread stops independently — the tracer must handle concurrent stops
- The tracer is single-threaded by design (one `waitpid` loop), which becomes a bottleneck for heavily multi-threaded tracees
- `PTRACE_SEIZE` (since Linux 3.4) — attach without stopping the tracee, allows `PTRACE_INTERRUPT` for asynchronous stop

### 2.6 Limitations

1. **Extreme performance overhead** — 4 context switches per syscall, 10-100x slowdown
2. **Single tracer per process** — only one process can ptrace another at a time
3. **Serialized tracing** — the tracer handles all threads sequentially via `waitpid`
4. **TOCTOU races** — time between inspecting and executing syscall allows argument changes
5. **Complex state machine** — entry/exit stops are indistinguishable; signal-delivery stops add complexity
6. **Memory access limitations** — `PEEKTEXT`/`POKETEXT` are word-at-a-time, slow for large data
7. **Security concerns** — a tracee under ptrace can potentially be exploited to escape sandboxes
8. **Not portable** — register layouts and behavior vary across architectures

---

## 3. LD_PRELOAD + Seccomp-BPF Hybrid

### 3.1 The Concept

Use LD_PRELOAD as the **fast path** (no kernel transitions for libc-wrapped syscalls) and seccomp-BPF as a **safety net** to catch any syscalls that bypass libc.

```
Application code
    ↓
libc wrapper (e.g., open()) ← intercepted by LD_PRELOAD shim
    ↓ (if not intercepted)
syscall instruction ← caught by seccomp-BPF filter
    ↓
kernel
```

### 3.2 Why LD_PRELOAD Alone Is Insufficient

LD_PRELOAD intercepts **libc function wrappers**, not actual syscalls. Bypasses:

1. **Direct `syscall` instructions** via inline assembly
2. **Statically linked binaries** — don't use the dynamic linker at all
3. **glibc internal inlining** — some syscall sequences are inlined within libc itself (local PLT)
4. **`io_uring`** — submission queue entries go directly to kernel, bypassing all userspace wrappers
5. **Go runtime** — makes syscalls directly without going through libc
6. **Rust `libc` crate** — can compile to direct syscall instructions

### 3.3 Why Seccomp-BPF Alone Is Insufficient

For a simulation sandbox, seccomp-BPF alone is limiting because:
- It cannot modify arguments or return values
- Each intercepted syscall (via `RET_TRACE` or `RET_USER_NOTIF`) requires expensive context switches
- Cannot dereference pointer arguments

### 3.4 The Hybrid Advantage

| Layer | What it catches | Performance | Can modify? |
|-------|----------------|-------------|:-----------:|
| LD_PRELOAD shim | libc wrappers (majority of syscalls) | Zero overhead (same-process function call) | Yes — full access to args and memory |
| Seccomp-BPF filter | All syscalls (kernel-level enforcement) | Low for allow/deny; expensive for notify | Limited — needs ptrace or unotify |

This is exactly the approach **Shadow simulator** uses.

### 3.5 Shadow Simulator Implementation

Shadow's architecture for syscall interception:

**Shim library** (LD_PRELOADed):
- Overrides libc symbols via dynamic linker precedence
- Hot-path syscalls (time, clock_gettime) handled **directly in the shim** using shared memory — zero IPC cost
- Other syscalls sent from shim to Shadow process via IPC control channel
- The shim blocks the managed process while Shadow handles the syscall

**Seccomp filter** (safety net):
- Catches any syscalls that bypass the shim
- Installed in the managed process
- Uses `SECCOMP_RET_TRAP` (SIGSYS) to redirect to the shim's signal handler

**Shared memory for hot-path optimization**:
- Time-related syscalls are the most frequent
- Shadow maintains simulated time in shared memory
- The shim reads this directly — no IPC, no context switch
- Shadow advances time when the process blocks on a syscall

**Memory access**:
- Some syscalls pass buffer addresses (e.g., `sendto()` buffer)
- The shim sends the address, not the contents
- Shadow uses `process_vm_readv`/`process_vm_writev` to directly access managed process memory

**Deterministic simulation**:
- All random bytes come from a seeded PRNG
- `getrandom()`, `/dev/urandom` reads are intercepted and emulated
- Time is fully virtual — controlled by Shadow
- Signals delivered deterministically

**Shim design constraints**:
- Minimizes libc usage — libc global state can be corrupted when the shim runs inside SIGSYS handlers
- Signal handlers are not reentrant and not async-signal-safe
- Implements syscall ABI directly, not libc wrappers

**Limitations of Shadow's approach**:
- Relies on LD_PRELOAD — doesn't work with fully static binaries without workarounds
- For static binaries, they considered: `fork()` → `exec()` → `ptrace()` → inject `dlopen("libshim.so")` → `ptrace(detach)`
- Busy loops can cause deadlock if the process never makes a blocking syscall (addressed with `--model-unblocked-syscall-latency`)

---

## 4. seccomp_unotify (User Notification)

### 4.1 Overview

Available since **Linux 5.0**. Allows a seccomp filter to delegate syscall handling to a separate **supervisor process** in userspace. Conceptually similar to FUSE for filesystems.

### 4.2 How It Works

```
1. Target installs seccomp filter with SECCOMP_FILTER_FLAG_NEW_LISTENER
   → returns a notification file descriptor

2. Supervisor receives the notification fd (via fork inheritance, SCM_RIGHTS, or pidfd_getfd)

3. Target makes a syscall that triggers SECCOMP_RET_USER_NOTIF
   → Target blocks (interruptible sleep)
   → Notification event generated on the fd

4. Supervisor calls ioctl(fd, SECCOMP_IOCTL_NOTIF_RECV, &notif)
   → Receives: syscall number, args, pid, id cookie

5. Supervisor decides what to do:
   a) Spoof a return value: SECCOMP_IOCTL_NOTIF_SEND with error/val
   b) Allow the syscall to proceed: SECCOMP_IOCTL_NOTIF_SEND with SECCOMP_USER_NOTIF_FLAG_CONTINUE
   c) Inject a file descriptor: SECCOMP_IOCTL_NOTIF_ADDFD

6. Target resumes with the spoofed or real result
```

### 4.3 Ioctl Operations

| Operation | Since | Purpose |
|-----------|-------|---------|
| `SECCOMP_IOCTL_NOTIF_RECV` | 5.0 | Read notification (blocks until available) |
| `SECCOMP_IOCTL_NOTIF_SEND` | 5.0 | Send response (spoof return or allow) |
| `SECCOMP_IOCTL_NOTIF_ID_VALID` | 5.0 | Check if notification is still valid (TOCTOU check) |
| `SECCOMP_IOCTL_NOTIF_ADDFD` | 5.9 | Inject file descriptor into target |

### 4.4 Response Types

**Spoofed return value**:
```c
struct seccomp_notif_resp resp = {
    .id = notif.id,
    .val = 0,           // return value
    .error = 0,         // errno (0 = success)
    .flags = 0,
};
ioctl(notify_fd, SECCOMP_IOCTL_NOTIF_SEND, &resp);
```

**Allow the real syscall to proceed** (since Linux 5.5):
```c
resp.flags = SECCOMP_USER_NOTIF_FLAG_CONTINUE;
ioctl(notify_fd, SECCOMP_IOCTL_NOTIF_SEND, &resp);
```
Useful when the supervisor needs to log or audit but still wants the real syscall to execute.

### 4.5 File Descriptor Injection (SECCOMP_IOCTL_NOTIF_ADDFD)

Available since **Linux 5.9**. Enables the supervisor to inject file descriptors into the target's fd table.

```c
struct seccomp_notif_addfd addfd = {
    .id = notif.id,
    .srcfd = local_fd,      // fd in supervisor
    .newfd = 0,              // 0 = kernel picks; or specific fd number
    .newfd_flags = 0,        // O_CLOEXEC etc.
    .flags = SECCOMP_ADDFD_FLAG_SEND,  // atomically add + respond (5.14+)
};
int target_fd = ioctl(notify_fd, SECCOMP_IOCTL_NOTIF_ADDFD, &addfd);
```

Use cases:
- Emulating `open()` / `openat()` — supervisor opens the file and injects the fd
- Emulating `socket()` — supervisor creates the socket and injects it
- Emulating `accept()` — supervisor accepts and injects the connected fd
- Redirecting stdout/stderr to log files
- `dup2()` emulation

**`SECCOMP_ADDFD_FLAG_SEND`** (since Linux 5.14): Atomically adds the fd AND sends the response, preventing fd leaks if the target is killed between addfd and send.

**`SECCOMP_ADDFD_FLAG_SETFD`**: Install at a specific fd number (like `dup2`). If the fd is already open, it is atomically closed and reused.

### 4.6 TOCTOU (Time-of-Check-Time-of-Use) Problem

**This is the critical security limitation of seccomp_unotify.**

Between when the target makes the syscall and when the supervisor inspects the arguments, another thread in the target can **rewrite the memory** that pointer arguments point to.

Example: Target calls `openat(AT_FDCWD, "/safe/path", O_RDONLY)`. Supervisor reads the path via `/proc/pid/mem` and sees `/safe/path`. Meanwhile, another thread rewrites the buffer to `/etc/shadow`. Supervisor says "allow". Kernel opens `/etc/shadow`.

Mitigations:
- `SECCOMP_IOCTL_NOTIF_ID_VALID` — check if the notification is still valid before responding
- Never use for security-critical decisions on pointer arguments
- The man page explicitly states: "This mechanism is explicitly not intended as a method for implementing security policy"
- Only use when the supervisor is **more privileged** than the target

For a simulation sandbox where the goal is **determinism** (not security against a hostile process), TOCTOU is less concerning — the simulated process is cooperative.

### 4.7 Performance Characteristics

| Metric | Approximate Value |
|--------|-------------------|
| Per-notification overhead | ~3-7 us |
| vs. ptrace | ~3x faster than ptrace for equivalent interception |
| vs. eBPF (proposed, not merged) | ~42x slower |
| vs. seccomp-bpf filter evaluation | ~100-1000x slower (filter is nanoseconds) |
| Context switches per intercepted syscall | 2 (target→supervisor, supervisor→target) |

The overhead is dominated by context switches between the target process and the supervisor. For hot-path syscalls, this is unacceptable. For infrequent syscalls (`mount`, `mknod`, `connect`), it is fine.

### 4.8 Advantages Over Ptrace

1. **Selective interception** — only syscalls returning `SECCOMP_RET_USER_NOTIF` trigger the notification; others proceed at native speed
2. **Fewer context switches** — 2 per intercepted syscall vs. 4 for ptrace
3. **No PTRACE_ATTACH** — doesn't require `CAP_SYS_PTRACE` or ptrace relationship
4. **Better structure** — clean request/response protocol vs. ptrace's complex state machine
5. **File descriptor injection** — `ADDFD` ioctl enables emulating fd-producing syscalls
6. **Can be used from a different process** — the supervisor doesn't need to be the parent

---

## 5. Real-World Implementations

### 5.1 Shadow Simulator

**Purpose**: Deterministic network simulation. Run real applications (Tor, Bitcoin nodes) in a simulated network.

**Interception strategy**: LD_PRELOAD shim + seccomp-BPF safety net (see [Section 3.5](#35-shadow-simulator-implementation)).

**Key design decisions**:
- Shared memory for time-related hot-path syscalls (zero IPC cost)
- Direct memory access via `process_vm_readv`/`process_vm_writev`
- Deterministic PRNG for all randomness
- Virtual time controlled by the simulator
- Minimized libc usage in the shim to avoid reentrancy issues

**Relevance to LinBox**: Shadow is the closest existing system to what LinBox needs. Their hybrid approach is directly applicable.

### 5.2 gVisor Sentry

**Purpose**: Application kernel that runs in userspace. Provides a Linux-compatible syscall interface without exposing the host kernel.

**Interception strategy**: Three platforms:

**Systrap (default since 2023)**:
- Uses `SECCOMP_RET_TRAP` to intercept ALL syscalls with SIGSYS
- Signal handler populates shared memory regions
- Sentry reads shared memory to determine what was requested
- Sentry handles the syscall and writes result back to shared memory
- Signal handler returns to the application
- **Optimization**: Patches `mov sysno, %eax; syscall` (7 bytes) with `jmp *%gs:offset` to skip the signal overhead for known patterns
- ~800ns per syscall overhead (vs. ~70-200ns native)

**KVM**:
- Uses hardware virtualization
- Application runs in guest ring 0
- Sentry runs as VMM
- Best performance but requires KVM access

**Ptrace (deprecated)**:
- `PTRACE_SYSEMU` to trap every syscall
- ~7us per syscall
- Replaced by Systrap

**Sentry self-sandboxing**:
- Applies seccomp-BPF to itself — only 53 host syscalls allowed (68 with networking)
- If the sentry tries to make an unauthorized syscall, it is killed
- File I/O goes through Gofers (separate processes)

**Performance**:
- Systrap: >2x throughput vs. ptrace on Node.js, >7x on WordPress
- KVM: ~800ns per syscall
- Ptrace: ~7us per syscall
- All platforms add overhead vs. native (2.2-2.8x for simple syscalls)

### 5.3 Firecracker

**Purpose**: Lightweight microVM for serverless/container workloads (AWS Lambda).

**Interception strategy**: seccomp-BPF as a **whitelist filter** (not for interception — for restriction).

**Implementation**:
- Requires only 37 out of 313 syscalls
- Default action: `SCMP_ACT_ERRNO` (deny with error)
- Filters loaded per-thread (VMM thread, VCPU threads)
- JSON-based filter definitions compiled at build time via `seccompiler`
- Jailer applies filters before guest boot (defense-in-depth)
- Custom filters supported but discouraged

**Performance note**: They identified that checking the most frequently used syscalls first in the BPF filter improves performance (BPF evaluates linearly).

**Relevance to LinBox**: Firecracker demonstrates how to use seccomp purely as a restriction mechanism. Less relevant for interception but shows best practices for filter construction.

### 5.4 Docker/Containerd Seccomp Profiles

**Purpose**: Restrict container syscalls to reduce host kernel attack surface.

**Implementation**:
- Default profile blocks ~44 syscalls out of 300+
- JSON profile format: `defaultAction: SCMP_ACT_ERRNO`, with explicit `SCMP_ACT_ALLOW` overrides
- Originally written by Jessie Frazelle for Docker; now used by Docker, Podman, CRI-O, containerd
- OCI runtime spec hooks for applying profiles

**Blocked syscall categories**:
- Kernel module loading (`init_module`, `delete_module`)
- I/O privilege modification (`iopl`, `ioperm`)
- Kernel keyring access (not namespaced)
- `kexec_load` (loading a new kernel)
- Various namespace and mount operations

**Relevance to LinBox**: Shows the ecosystem standard for syscall allowlisting. Docker's default profile is a reasonable starting point for what "normal" applications need.

### 5.5 Bubblewrap (bwrap)

**Purpose**: Unprivileged sandboxing tool. Used by Flatpak.

**Implementation**:
- Creates new mount namespace with tmpfs root
- Applies seccomp filters passed via file descriptor (`--seccomp FD`)
- Does NOT generate filters itself — the caller provides compiled BPF
- Combines with PID, network, IPC namespaces
- Trivial pid1 inside the sandbox for child reaping

**Practical usage**:
- Anthropic's `sandbox-runtime` uses bubblewrap with pre-generated seccomp filters
- Their BPF filter intercepts `socket()` to block `AF_UNIX` socket creation

**Relevance to LinBox**: Bubblewrap is a useful building block for creating the sandbox environment (namespaces, mount isolation), with custom seccomp filters on top.

---

## 6. Performance Comparison

### 6.1 Summary Table

| Mechanism | Context Switches | Per-Syscall Overhead | Modify Args? | Modify Return? | Scope |
|-----------|:---:|---|:---:|:---:|---|
| Native syscall | 0 | ~70-200 ns | N/A | N/A | — |
| seccomp-BPF filter (cached) | 0 | ~tens of ns | No | Only errno | Kernel |
| seccomp-BPF filter (linear) | 0 | ~1-6 us | No | Only errno | Kernel |
| LD_PRELOAD shim | 0 | ~0 (function call) | Yes | Yes | Userspace (libc only) |
| seccomp `RET_TRAP` + SIGSYS | 0 (same process) | ~hundreds of ns | Yes (in handler) | Yes (in handler) | Kernel + same process |
| `SECCOMP_RET_USER_NOTIF` | 2 | ~3-7 us | No (TOCTOU) | Yes (spoof) | Cross-process |
| `SECCOMP_RET_TRACE` + ptrace | 2 | ~3-5 us | Yes | Yes | Cross-process |
| ptrace `PTRACE_SYSCALL` | 4 | ~5-9 us | Yes | Yes | Cross-process |
| ptrace `PTRACE_SYSEMU` | 2 | ~3-7 us | Yes | Yes | Cross-process |

### 6.2 Key Insights for Sandbox Design

1. **LD_PRELOAD is fastest** for the common case (~zero overhead) but cannot catch all syscalls
2. **seccomp `RET_TRAP` + SIGSYS** (gVisor Systrap) gives same-process interception with ~hundreds of ns overhead — a good middle ground
3. **seccomp_unotify** is 3x faster than ptrace but still microseconds per syscall — only suitable for infrequent syscalls
4. **ptrace is the most flexible** (full register control) but the slowest (5-9 us per syscall, 10-100x total slowdown)
5. **The hybrid approach** (fast path + safety net) is the optimal architecture

---

## 7. Implications for LinBox

### 7.1 Recommended Architecture

Based on this research, the optimal syscall interception strategy for a deterministic simulation sandbox:

**Layer 1 — LD_PRELOAD shim (fast path)**:
- Intercepts libc wrappers for common syscalls
- Hot-path operations (time, randomness) handled via shared memory — zero IPC
- Covers ~95% of syscalls for dynamically linked applications
- Zero overhead (same-process function call)

**Layer 2 — seccomp-BPF filter (safety net)**:
- `SECCOMP_RET_TRAP` (SIGSYS) for syscalls that bypass the shim
- Signal handler in the shim redirects to the interception logic
- Same-process handling — no context switch to supervisor
- Covers the remaining ~5% of direct syscalls

**Layer 3 — seccomp_unotify (optional, for complex cases)**:
- For syscalls that need supervisor involvement (fd injection, complex emulation)
- Use sparingly — only for infrequent operations
- `SECCOMP_IOCTL_NOTIF_ADDFD` for emulating `open()`, `socket()`, `accept()`

### 7.2 Key Design Decisions

1. **Shared memory for time** — Shadow proved this works. Virtual time in shared memory, shim reads directly
2. **Minimize shim libc usage** — avoid reentrancy issues in signal handlers; use raw syscall wrappers
3. **Deterministic PRNG** — intercept `getrandom()`, `/dev/urandom`, `rdtsc`/`rdtscp`
4. **Handle io_uring** — block or intercept `io_uring_setup` via seccomp; io_uring bypasses both LD_PRELOAD and traditional seccomp
5. **Static binary support** — use ptrace to inject the shim (Shadow's proposed approach), or require dynamic linking
6. **Multi-thread coordination** — track thread creation/destruction; control scheduling for determinism

### 7.3 Kernel Version Requirements

| Feature | Minimum Kernel |
|---------|---------------|
| seccomp-BPF | 3.5 |
| `SECCOMP_RET_TRACE` | 3.5 |
| `SECCOMP_RET_USER_NOTIF` | 5.0 |
| `SECCOMP_USER_NOTIF_FLAG_CONTINUE` | 5.5 |
| `SECCOMP_IOCTL_NOTIF_ADDFD` | 5.9 |
| `SECCOMP_ADDFD_FLAG_SEND` (atomic) | 5.14 |
| seccomp constant-action bitmap cache | 5.11 |
| Syscall User Dispatch (SUD) | 5.11 |

For LinBox, targeting **Linux 5.14+** provides all features including atomic fd injection.

---

## Sources

- [Linux kernel seccomp-BPF documentation](https://docs.kernel.org/userspace-api/seccomp_filter.html)
- [seccomp(2) man page](https://man7.org/linux/man-pages/man2/seccomp.2.html)
- [seccomp_unotify(2) man page](https://man7.org/linux/man-pages/man2/seccomp_unotify.2.html)
- [ptrace(2) man page](https://man7.org/linux/man-pages/man2/ptrace.2.html)
- [Shadow simulator design](https://shadow.github.io/docs/guide/design_2x.html)
- [Shadow — Co-opting Linux Processes for High-Performance Network Simulation (USENIX ATC '22)](https://www.usenix.org/system/files/atc22-jansen.pdf)
- [gVisor Systrap release](https://gvisor.dev/blog/2023/04/28/systrap-release/)
- [gVisor seccomp optimization](https://gvisor.dev/blog/2024/02/01/seccomp/)
- [gVisor architecture guide](https://gvisor.dev/docs/architecture_guide/platforms/)
- [Firecracker seccomp documentation](https://github.com/firecracker-microvm/firecracker/blob/main/docs/seccomp.md)
- [Christian Brauner — The Seccomp Notifier](https://brauner.io/2020/07/23/seccomp-notify.html)
- [Docker seccomp security profiles](https://docs.docker.com/engine/security/seccomp/)
- [Bubblewrap repository](https://github.com/containers/bubblewrap)
- [nullprogram.com — Intercepting and Emulating Linux System Calls with Ptrace](https://nullprogram.com/blog/2018/06/23/)
- [Alfonso Sanchez-Beato — Modifying System Call Arguments with ptrace](https://www.alfonsobeato.net/c/modifying-system-call-arguments-with-ptrace/)
- [strace --seccomp-bpf optimization](https://pchaigno.github.io/strace/2019/10/02/introducing-strace-seccomp-bpf.html)
- [seccomp_unotify as an alternative to ptrace (Golang)](https://medium.com/@mindo.robert1/using-seccomp-user-notifications-seccomp-unotify-as-an-alternative-to-ptrace-for-syscall-1c806a3e2960)
- [Nexpoline — Making syscall a Privilege not a Right](https://arxiv.org/html/2406.07429v1)

---

[← Back](README.md)
