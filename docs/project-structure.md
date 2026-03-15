# Project Structure

## The Big Picture

LinBox — это два процесса + инфраструктура вокруг них:

```
┌─────────────────────────────────────────────────────────┐
│  Docker Container (sandbox)                              │
│                                                          │
│  ┌────────────────────────────────────────────────────┐  │
│  │  Real Service (PostgreSQL, Redis, Node.js, ...)    │  │
│  │  Unmodified. Doesn't know it's being controlled.   │  │
│  └──────────────┬─────────────────────────────────────┘  │
│                 │ every libc call goes through...        │
│  ┌──────────────▼─────────────────────────────────────┐  │
│  │  liblinbox.so  (the shim)                          │  │
│  │  Loaded via LD_PRELOAD before the service starts.  │  │
│  │  Intercepts: time, random, network, filesystem,    │  │
│  │  signals, process creation — everything.           │  │
│  └──────────────┬─────────────────────────────────────┘  │
│                 │                                        │
└─────────────────┼────────────────────────────────────────┘
                  │  shared memory (fast, ~5ns)
                  │  unix socket (control, ~5μs)
┌─────────────────▼────────────────────────────────────────┐
│  linbox-controller                                       │
│  Runs outside the sandbox. Owns the truth about:         │
│  - what time it is (virtual time)                        │
│  - what seed to use (deterministic random)               │
│  - network rules (latency, partitions)                   │
│  - filesystem policies                                   │
│  Also manages: tc/netem, cgroups, CRIU snapshots         │
└──────────────────────────────────────────────────────────┘
```

That's it. The service thinks it talks to a real OS. The shim quietly replaces reality with whatever the controller says.

## How They Talk

Two channels, chosen by speed requirements:

```
                    Shared Memory (~6KB, mmap'd file)
                    ┌──────────────────────────────┐
Controller writes → │ time: 2025-01-01 00:00:00    │ ← Shim reads
                    │ seed: 0xDEADBEEF             │    (just a memory load,
                    │ net policy: allow, 50ms       │     no IPC, no syscall,
                    │ fs policy: fsync=noop          │     ~5 nanoseconds)
                    │ dns: db.local → 10.0.0.5      │
                    │ hostname: "box-pg-01"          │
                    └──────────────────────────────┘

                    Unix Socket (for everything else)
Controller ←──SBP──→ Shim
                    │ HELLO (shim registers on startup)
                    │ REGISTER_PROCESS (after fork)
                    │ REGISTER_TIMER (nanosleep)
                    │ ASK: connect to 10.0.0.5:5432?
                    │ etc.
```

~98% of intercepted calls are resolved locally from shared memory. Only ~2% need a round-trip to the controller.

## Code Layout

### `src/shim/` — the interception library

Compiles into `liblinbox.so`. Loaded into the target process via `LD_PRELOAD`. Every `.c` file here corresponds to a group of libc functions being intercepted:

| File | What it intercepts | Story |
|------|-------------------|-------|
| `linbox.c` | Constructor, initialization, state management | S01 |
| `linbox.h` | Internal shared header | S01 |
| `resolve.h` | `dlsym(RTLD_NEXT)` macros (how we find the "real" functions) | S01 |
| `time.c` | `clock_gettime`, `gettimeofday`, `time`, `clock`, `times` | S01 |
| `random.c` | `getrandom`, `open(/dev/urandom)`, `arc4random`, `rand` | S03 |
| `prng.c` | ChaCha20 PRNG engine (generates deterministic bytes) | S03 |
| `seccomp.c` | BPF filter installation (catch direct syscalls) | S04 |
| `sigsys.c` | SIGSYS handler (dispatches trapped syscalls) | S04 |
| `syscall-wrap.c` | Intercept the `syscall()` libc wrapper | S04 |
| `process.c` | `fork`, `clone`, `execve`, `posix_spawn` | S05 |
| `sleep.c` | `nanosleep`, `sleep`, `alarm`, `setitimer` | S07 |
| `timer-queue.c` | Virtual timer queue (min-heap) | S07 |
| `timerfd.c` | `timerfd_create/settime/gettime` | S07 |
| `posix-timer.c` | `timer_create/settime/gettime/delete` | S07 |
| `timed-wait.c` | `pthread_cond_timedwait`, `sem_timedwait` | S07 |
| `network.c` | `socket`, `connect`, `accept`, `send`, `recv` | S08 |
| `dns.c` | `getaddrinfo`, `gethostbyname` | S08 |
| `io-mux.c` | `poll`, `select`, `epoll` (timeout adjustment) | S08 |
| `fs-time.c` | `stat`, `fstat` (virtual timestamps) | S10 |
| `fs-sync.c` | `fsync`, `fdatasync` (configurable: real/noop/delay) | S10 |
| `fs-open.c` | `open`, `openat` (path remapping) | S10 |
| `signal.c` | `sigaction`, `sigprocmask`, `kill` | S11 |
| `seccomp-unotify.c` | Notification fd setup for cross-process interception | S12 |
| `sysinfo.c` | `uname`, `sysinfo`, `gethostname`, `getrusage` | S13 |
| `arch/` | Architecture abstraction (x86-64 vs ARM64 registers) | S17 |

The pattern in every file is the same:
```c
// 1. Lazy-resolve the real function
static int (*real_func)(...) = NULL;

// 2. Define our replacement
int func(...) {
    if (!real_func) real_func = dlsym(RTLD_NEXT, "func");
    // 3. Read policy from shared memory, return virtual result
}
```

### `src/controller/` — the controller process

Compiles into `linbox-controller` binary. Runs outside the sandbox. Manages the simulation.

| File | What it does | Story |
|------|-------------|-------|
| `main.c` | Event loop, unix socket server, SBP message dispatch | S02 |
| `time-manager.c` | Virtual time → writes to shared memory | S02 |
| `network-sim.c` | tc/netem rules, iptables partitions, veth pairs | S09 |
| `cgroup-manager.c` | CPU/memory/IO limits via cgroups v2 | S09 |
| `unotify-handler.c` | seccomp_unotify supervisor (fd injection) | S12 |
| `snapshot.c` | CRIU checkpoint coordination | S15 |
| `restore.c` | CRIU restore coordination | S15 |
| `observability.c` | Metrics collection, SBP GET_METRICS | S16 |

### `src/common/` — shared between shim and controller

Both sides need to agree on message formats and memory layout.

| File | What it is |
|------|-----------|
| `sbp.h` / `sbp.c` | SBP wire protocol: message types, serialization |
| `shm-layout.h` | Shared memory structure (~6KB): time, seed, network/fs/dns policies |
| `shm.c` | mmap create/attach/detach helpers |

### `src/proxy/` — wire protocol proxies

Transparent proxies for application-level protocol interception (Layer 4).

| File | What it does | Story |
|------|-------------|-------|
| `pg-proxy.c` | Postgres wire protocol parser + proxy | S14 |
| `tproxy-setup.c` | TPROXY iptables rule management | S14 |

### `src/ebpf/` — eBPF programs

Kernel-level monitoring and packet inspection.

| File | What it does | Story |
|------|-------------|-------|
| `syscall-monitor.bpf.c` | Tracepoint program: syscall counters + latency | S16 |
| `syscall-monitor.c` | Userspace loader and metric reader | S16 |
| `tc-inspect.c` | TC program for packet inspection | S14 |

### Everything else

```
docker/
  Dockerfile.sandbox          # base image with liblinbox.so
  Dockerfile.controller       # controller image
  Dockerfile.pg-sandbox       # PostgreSQL + liblinbox.so
  Dockerfile.arm64            # multi-arch build
  seccomp-profile.json        # Docker seccomp profile

scripts/
  setup-linux.sh              # install dev dependencies
  run-sandbox.sh              # manual testing
  setup-bridge.sh             # LinBox bridge network
  snapshot.sh                 # create/restore snapshots

tests/e2e/
  pg-time.sh                  # SELECT now() = virtual time
  pg-random.sh                # deterministic uuid_generate_v4()
  pg-determinism.sh           # two runs = identical results

docker-compose.yml            # controller + sandbox
CMakeLists.txt                # build system
Makefile                      # convenience wrapper
.clang-format                 # code style
```

## Build Outputs

| Target | What it produces | How to use |
|--------|-----------------|-----------|
| `make build` | `build/liblinbox.so` | `LD_PRELOAD=./build/liblinbox.so your-service` |
| `make build` | `build/linbox-controller` | Runs as a separate process or in a container |
| `make proxy` | `build/pg-proxy` | Runs inside network namespace |
| `make monitor` | `build/syscall-monitor` | Requires root/CAP_BPF |
| `make test` | Runs unit tests | |
| `make e2e` | Runs E2E tests via docker-compose | |

---

[← Back](README.md)
