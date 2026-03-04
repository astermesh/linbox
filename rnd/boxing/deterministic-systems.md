# Deterministic Simulation Systems: Technical Deep Dive

In-depth technical research into how production-grade deterministic simulation systems achieve determinism, what they intercept, their performance characteristics, and their trade-offs.

## Table of Contents

1. [Antithesis](#1-antithesis)
2. [Shadow (shadow-rs)](#2-shadow-shadow-rs)
3. [FoundationDB Simulation (Sim2)](#3-foundationdb-simulation-sim2)
4. [rr (Record and Replay)](#4-rr-record-and-replay)
5. [Hermit (Meta)](#5-hermit-meta)
6. [MadSim](#6-madsim)
7. [TigerBeetle](#7-tigerbeetle)
8. [Comparison Matrix](#8-comparison-matrix)

---

## 1. Antithesis

**What it is:** A commercial platform that runs arbitrary containerized software inside a custom deterministic hypervisor ("The Determinator"), enabling fully reproducible simulation testing with fault injection.

**Founded:** 2018, by the same team that built FoundationDB. Launched out of stealth in 2024. Raised $105M Series A in December 2025 led by Jane Street.

### 1.1 The Deterministic Hypervisor ("The Determinator")

**Base technology:** Custom fork of FreeBSD's [bhyve](https://en.wikipedia.org/wiki/Bhyve) hypervisor. They chose bhyve for its permissive BSD license, clean architecture, and mature VMX support. They started by *removing* standard hypervisor functionality, then building deterministic behavior incrementally on top of a minimal foundation.

**Not QEMU/KVM.** The hypervisor is built on FreeBSD + bhyve, not on Linux + QEMU + KVM. This is a deliberate choice: bhyve's codebase is smaller and more tractable for the kind of deep modifications they needed.

**Hardware virtualization:** Uses Intel VMX (Virtual Machine Extensions) for hardware-assisted virtualization. Targets modern Intel server CPUs.

### 1.2 How Determinism Is Achieved

#### Single vCPU Per VM

Each VM instance runs on exactly **one physical CPU core**. On a 48-96 core server, they spawn that many separate VMs, each pinned to a different core, exploring different state-space paths in parallel. This eliminates:
- Thread scheduling non-determinism
- Memory ordering non-determinism (everything is sequentially consistent on one core)
- Inter-core cache coherence effects

Parallelism comes from running *many VMs* across cores, not from multi-core within a single VM.

#### Time Virtualization

They impose **time determinism**: the guest's simulated clock values are a pure function of the deterministic state and execution history of the guest system.

Every time source is intercepted and virtualized:
- **TSC (Time Stamp Counter)** — trapped by the hypervisor, returns virtual time
- **HPET (High Precision Event Timer)** — returns virtual time
- **All other hardware timers** — hidden from the guest

They attempted to peg simulated time to instruction counting but hit fundamental limitations:
- **PMC (Performance Monitoring Counter) precision**: approximately one in a trillion instructions is miscounted due to undocumented CPU quirks
- **Interrupt delivery jitter**: PMC threshold notifications via APIC result in dozens of instructions executing before the CPU is actually notified, with variable overhead measured in CPU cycles

These limitations mean instruction-level determinism via PMC is not perfectly reliable, requiring additional compensation mechanisms in the hypervisor.

#### I/O Determinism

Guest-to-hypervisor communication uses the **VMCALL instruction** (Intel x86 context-switch facility), enabling bidirectional data flow. The guest can emit data and ingest commands or RNG seeds that control guest behavior. Points where the guest ingests input become possible branch points for future state-space exploration.

**Network:** All containers run inside a single VM on a single host. Inter-container networking goes through simulated TCP on localhost within the guest. No external network connectivity exists.

**Disk:** Simulated within the deterministic environment. All I/O is controlled by the hypervisor.

**Randomness:** `/dev/random` and `/dev/urandom` are replaced with special devices whose entropy is entirely provided by the Antithesis platform (deterministic PRNG seeded by the hypervisor).

### 1.3 Guest Environment

- **Guest OS:** Linux with a **mostly-stock 6.1 kernel** (custom kernels can be accommodated)
- **Guest CPU:** Simulated Intel x86-64 with Skylake-era extensions
- **Memory:** 10 GB default (configurable)
- **Customer software:** Runs as Docker/OCI containers on the guest Linux, defined via `docker-compose.yaml`
- **No nested hardware virtualization** (software emulation only)

The Antithesis SDK is injected at runtime as `/usr/lib/libvoidstar.so`.

### 1.4 State Exploration and Replay

The exploration model uses a **multiverse tree**:
- Inside the guest, execution follows a single linear history
- Outside, the hypervisor sees a tree of all execution paths ever visited
- When interesting/rare behavior is detected (increased code coverage, rare log messages), the system **snapshots the entire VM state** and branches to explore multiple futures from that point
- This branching happens thousands of times per test run

**Snapshot mechanism:** Advanced VM snapshotting allows the entire state (CPU registers, memory, disk) to be saved and restored instantaneously. This is a core capability built into their bhyve fork.

**Replay:** The system supports "time-travel debugging with artifact extraction." Specific replay format and recording details have not been publicly disclosed.

### 1.5 Performance

- Prioritizes **throughput over latency**: exploring many paths in parallel matters more than wall-clock speed of individual VMs
- **Faster-than-real-time:** Sleep calls (`time.Sleep()`, `nanosleep`, etc.) do not actually wait — the hypervisor advances virtual time instantly
- Avoiding inter-core synchronization provided a "significant performance advance"
- Debug logging generates ~**50 GB of output per 20-minute run** during development
- Specific overhead numbers not publicly disclosed, but described as practical for continuous testing

### 1.6 Pricing

- **On-demand:** $2/hour per CPU core
- **Annual reserved:** Up to 30% discount
- **Enterprise range:** $20,000 - $100,000+/year depending on scale
- Available on AWS Marketplace
- Pricing scales with compute resources dedicated to testing

### 1.7 Trade-offs

| Advantage | Disadvantage |
|-----------|-------------|
| Works with any containerized software — no code changes required | Commercial, closed-source (expensive) |
| Complete determinism including kernel, drivers, everything | Single vCPU means no multi-core concurrency bugs can manifest within one VM |
| Full VM snapshot/restore for multiverse exploration | Integration complexity (must containerize everything) |
| Faster-than-real-time execution | Guest limited to x86-64, Intel CPUs |
| Fault injection at network, process, timing level | 10 GB memory limit per VM (configurable) |

**Key sources:**
- [So you think you want to write a deterministic hypervisor?](https://antithesis.com/blog/deterministic_hypervisor/)
- [Antithesis: Pioneering Deterministic Hypervisors with FreeBSD and Bhyve](https://freebsdfoundation.org/antithesis-pioneering-deterministic-hypervisors-with-freebsd-and-bhyve/)
- [The Antithesis Environment](https://antithesis.com/docs/environment/the_antithesis_environment)
- [How Antithesis Works](https://antithesis.com/product/how_antithesis_works/)
- [Antithesis Pricing](https://antithesis.com/pricing/)
- [2024 FreeBSD Developer Summit: Antithesis deterministic hypervisor](https://freebsdfoundation.org/blog/2024-freebsd-developer-summit-antithesis-deterministic-hypervisor/)

---

## 2. Shadow (shadow-rs)

**What it is:** An open-source discrete-event network simulator that directly executes real application code (unmodified binaries) by intercepting and emulating Linux syscalls. Originally developed for Tor network research. Over 200 academic citations.

**Repository:** [github.com/shadow/shadow](https://github.com/shadow/shadow)

### 2.1 Interception Mechanism: LD_PRELOAD + seccomp-bpf Hybrid

Shadow uses a **two-layer** syscall interception strategy:

#### Layer 1: LD_PRELOAD (fast path)

A **shim library** is injected into every managed process via `LD_PRELOAD`. Because the shim is loaded first, it wins symbol resolution for all dynamically linked calls. The shim overrides libc syscall wrapper functions (e.g., `send()`, `recv()`, `clock_gettime()`) with alternative implementations that route to Shadow's simulated kernel.

**Limitation:** LD_PRELOAD only works for dynamically linked function calls. It does NOT intercept:
- Statically linked function calls (e.g., calls within libc itself)
- Direct `syscall` instruction invocations
- 100% statically linked binaries (LD_PRELOAD is completely ignored)

#### Layer 2: seccomp-bpf (fallback)

For syscalls that escape LD_PRELOAD, the shim installs a **seccomp-bpf filter** that traps all syscalls not originating from the shim itself, plus a handler function to process them. The seccomp filter runs in kernel mode with "very small overhead."

**Performance hierarchy:** Preloading is significantly faster than seccomp alone (and both are faster than ptrace), because preloading avoids the mode transition and seccomp filter execution overhead.

### 2.2 Inter-Process Shared Memory

Shadow must read/write managed process memory for syscalls that pass buffer addresses (e.g., `sendto()` buffer). Rather than copying data back and forth:
- The shim sends the buffer *address* (not contents) to Shadow
- Shadow uses an **inter-process memory access manager** that remaps managed process memory into shared files
- This enables direct memory access between Shadow and its managed processes without extraneous data copies or control messages

### 2.3 Threading Model

**Worker threads:** Shadow uses one worker thread per virtual host, but limits parallel worker threads to the number of available CPU cores (avoids oversubscription). Uses **work stealing** for load balancing and **CPU pinning** to reduce cache misses and context switches.

**Managed process threads:** A critical design choice: Shadow runs either a worker thread OR one of its managed processes, but **never both at the same time**. Each worker and all its managed processes are pinned to the same physical core.

**Time advancement for threads:**
- By default, Shadow runs each managed thread until it blocks on a syscall (`nanosleep`, `read`, `select`, `futex`, `epoll`, etc.)
- Time only advances when all threads are blocked
- CPU is modeled as **infinitely fast** (sufficient for non-CPU-bound network applications)

**Busy loop mitigation:**
- `--model-unblocked-syscall-latency`: advances time slightly on every syscall and VDSO call, switches threads if another becomes runnable
- `--native-preemption-enabled` (experimental): uses native Linux timers to preempt threads that make no syscalls at all

### 2.4 Determinism

Determinism is achieved through:
- **Seeded PRNG for all randomness**: `getrandom()`, `/dev/random`, `/dev/urandom` all use seeded deterministic sources
- **Auxiliary vector randomness**: Shadow overwrites the 16 bytes of random data the kernel provides in the auxiliary vector (critical for Go programs)
- **CPUID trapping**: On supported Intel CPUs, Shadow traps `cpuid` to report that `rdrand`/`rdseed` instructions are unavailable, forcing software to fall back to interceptable randomness sources

Same inputs + same seed = same operation sequence.

### 2.5 Simulated Kernel

Shadow's simulated kernel re-implements:
- **Time management** (virtual discrete-event time)
- **File descriptor I/O** (file, socket, pipe, timer, event descriptors)
- **Signals**
- **TCP and UDP** transport protocols (full simulated implementations)
- **Network routing, queuing, and bandwidth management** via configurable network graph (path latency, packet loss)

### 2.6 Supported Syscalls (164+)

The Phantom paper (merged into Shadow v2.0+) implements **164 syscalls** across categories:

| Category | Examples |
|----------|----------|
| **Networking** | `socket`, `bind`, `listen`, `accept`, `connect`, `send`/`recv` variants, `sendto`, `sendmsg`, `recvfrom`, `recvmsg`, `shutdown`, `getsockopt`, `setsockopt` |
| **File I/O** | `read`, `write`, `open`, `close`, `stat`, `fstat`, `lseek`, `ioctl`, `fcntl`, `dup`, `dup2`, `openat`, `getdents64`, `faccessat`, `close_range` |
| **Memory** | `mmap`, `munmap`, `mprotect`, `brk`, `mremap` |
| **Process** | `fork`, `vfork`, `execve`, `clone`, `clone3`, `exit`, `exit_group`, `wait4`, `waitid` |
| **Epoll/Polling** | `epoll_create`, `epoll_ctl`, `epoll_wait`, `poll`, `ppoll`, `select`, `pselect6` |
| **Signals** | `rt_sigaction`, `rt_sigprocmask`, `rt_sigreturn`, `kill`, `tgkill`, `sigaltstack` |
| **Time** | `clock_gettime`, `gettimeofday`, `nanosleep`, `clock_nanosleep`, `timerfd_create`, `timerfd_settime` |
| **Events** | `eventfd`, `eventfd2`, `pipe`, `pipe2` |
| **Randomness** | `getrandom` |
| **Scheduling** | `sched_yield`, `sched_getaffinity`, `rseq` |
| **Thread sync** | `futex`, `set_robust_list`, `get_robust_list` |
| **Misc** | `prctl`, `arch_prctl`, `set_tid_address`, `getpid`, `gettid`, `getuid`, `getgid`, `uname` |

Unsupported syscalls return `ENOSYS` with a warning log. Many applications recover gracefully. `vfork` is implemented as a synonym for `fork`.

### 2.7 Performance

- Thread-per-host with work stealing and CPU pinning
- Preloading fast path avoids mode transitions for most syscalls
- Specific benchmark numbers vary by workload; the Phantom paper won Best Paper at USENIX ATC 2022
- Shadow can simulate thousands of network-connected processes on a single machine

### 2.8 Trade-offs

| Advantage | Disadvantage |
|-----------|-------------|
| Runs real unmodified binaries | Does not work with 100% statically linked binaries |
| Full network simulation with configurable topology | CPU modeled as infinitely fast (not suitable for CPU-bound workloads) |
| Deterministic with seeded PRNG | Busy loops can cause deadlock without special options |
| Open source, well-documented | 164 syscalls covered, but long tail of unsupported features |
| Low overhead via LD_PRELOAD fast path | Limited to Linux x86-64 |

**Key sources:**
- [Shadow Design Overview](https://shadow.github.io/docs/guide/design_2x.html)
- [Shadow GitHub](https://github.com/shadow/shadow)
- [Phantom Paper (USENIX ATC 2022)](https://www.usenix.org/system/files/atc22-jansen.pdf)
- [Shadow Known Limitations](https://shadow.github.io/docs/guide/limitations.html)

---

## 3. FoundationDB Simulation (Sim2)

**What it is:** A built-in deterministic simulation framework that runs the *entire real FoundationDB database* in a single-threaded process with simulated network, disk, and time. Pioneered deterministic simulation testing (DST) around 2010.

### 3.1 The Flow Language

Flow is a C++ extension developed in the first weeks of the FoundationDB project. It adds ~10 keywords to C++11 and acts as a transpiler: Flow code compiles to raw C++11, then to native binary via standard toolchain.

**Key keywords:**
- **`ACTOR`**: marks functions that support `wait()` suspension points (cooperative coroutines)
- **`state`**: preserves variables across multiple `wait()` calls within an actor
- **`wait()`**: suspends the actor until a `Future<T>` resolves

The `actorcompiler.h` preprocessor transforms Flow code into state machines. Functionally equivalent to Rust async/await but predates modern coroutine support.

**Why single-threaded:** All actors run cooperatively on a single thread. Actors yield at `wait()` points. When all actors are waiting, the event loop advances simulated time to the next scheduled event. This guarantees deterministic execution order.

### 3.2 Interface Polymorphism (The `g_network` Swap)

The core trick is a global pointer swap:

```
g_network -> INetwork interface
  |-- Production: Net2 (real TCP via Boost.ASIO)
  |-- Simulation: Sim2 (in-memory buffers)
```

The same `fdbserver` binary runs in both modes. No mocks. No stubs. Real transaction logs, real storage engines (RocksDB, Redwood), real Paxos consensus. The only difference is which implementation backs the interfaces.

### 3.3 Virtualized Interfaces

| Interface | Production Implementation | Simulation Implementation |
|-----------|--------------------------|--------------------------|
| **INetwork** | **Net2** — real TCP connections via Boost.ASIO, epoll-based run loop | **Sim2** — in-memory buffers, time-based simulation loop |
| **IConnection** | Real socket (via `Reference<IConnection>`) | **Sim2Conn** — `std::deque<uint8_t>` in-memory buffer |
| **IAsyncFile** | Real disk I/O with `OPEN_ATOMIC_WRITE_AND_CREATE`, `OPEN_LOCK`, etc. | Simulated disk with latency modeling, space limits, corruption injection |
| **Time** | Wall-clock time | Simulated clock that advances in discrete events |
| **Randomness** | System PRNG | `deterministicRandom()` — seeded PRNG for reproducibility |

Additionally:
- **Network latency** — Sim2 adds `delay()` calls with values from `deterministicRandom()`
- **Drive performance** — simulation models drive performance per machine, including drive space and fill-up scenarios
- **Machine state** — processes can reboot with disk swaps or complete data loss (`RebootAndDelete`); 75% of the time under BUGGIFY, a rebooting machine gets random disks from the datacenter pool

### 3.4 BUGGIFY: Chaos Injection

BUGGIFY is compiled into the **production binary** but only activates in simulation:

**Three rules:**
1. Only evaluates to `true` in simulation mode
2. Each BUGGIFY site is enabled or disabled for the entire simulation run (consistent per-run)
3. Enabled sites fire with 25% probability (configurable via `BUGGIFY_WITH_PROB(p)`)

**Injection patterns:**
- **Minimal work reduction**: skip optional async tasks (e.g., use minimal Paxos quorum instead of querying all coordinators)
- **Error forcing**: append `|| BUGGIFY` to rare condition checks, forcing error handlers to execute
- **Delay injection**: `if (BUGGIFY) wait(delay(5));` to stress concurrent operations
- **Timeout shrinking**: production timeout 60s becomes 0.1s under BUGGIFY (600x shorter)
- **Knob randomization**: 748+ configuration knobs get randomized values when `if (randomize && BUGGIFY)` evaluates to true

**Recovery phase:** After 300 simulated seconds, `g_simulator.speedUpSimulation` suppresses certain BUGGIFY lines to prioritize recovery verification over chaos injection.

### 3.5 SimulatedCluster

Builds complete clusters in memory:
- 1-5 datacenters, 1-10+ machines per DC
- Real `fdbserver` code on simulated network
- Machine actor loops that reboot processes after random delays
- Network partitions, coordinator changes, disk failures

### 3.6 Testing Results

- **First 18 months:** FoundationDB never sent a single packet over a real network — developed entirely in simulation
- **Scale:** ~1 trillion CPU-hours equivalent of simulation testing
- **CI:** Every PR triggers hundreds of thousands of simulation tests on hundreds of cores for hours before human review
- **Nightly:** Tens of thousands of additional simulation runs with different seeds

**Verification strategies:**
1. **Reference implementation**: mirror operations in `std::map`, compare results
2. **Operation logging**: log every action to separate keyspace, replay to verify
3. **Invariant tracking**: mathematical guarantees (e.g., Cycle test maintains exactly N nodes in a ring)

### 3.7 Trade-offs

| Advantage | Disadvantage |
|-----------|-------------|
| Tests *real* production code, not mocks | Requires the Flow language (C++ extension) — cannot test arbitrary binaries |
| Trillion CPU-hour equivalent testing | Tightly coupled to FDB — no other project has reused the framework |
| BUGGIFY compiled into production binary for zero-cost fault injection paths | Single-threaded: cannot find true multi-core concurrency bugs |
| Deterministic replay from seed | Must design entire system around simulation from day one |
| Years of uptime compressed into seconds | Flow language is a barrier to new contributors |

**Key sources:**
- [Diving into FoundationDB's Simulation Framework](https://pierrezemb.fr/posts/diving-into-foundationdb-simulation/)
- [FoundationDB Simulation and Testing Docs](https://apple.github.io/foundationdb/testing.html)
- [BUGGIFY Deep Dive](https://transactional.blog/simulation/buggify)
- [FoundationDB Engineering Docs](https://apple.github.io/foundationdb/engineering.html)
- [FoundationDB GitHub](https://github.com/apple/foundationdb)

---

## 4. rr (Record and Replay)

**What it is:** A lightweight tool for recording, replaying, and debugging execution of Linux user-space applications. Developed at Mozilla for capturing low-frequency nondeterministic test failures in Firefox.

**Repository:** [github.com/rr-debugger/rr](https://github.com/rr-debugger/rr)

### 4.1 Interception Mechanism

**Dual-layer interception:**
1. **ptrace** — captures syscall results and signals (traditional debugger mechanism)
2. **seccomp-bpf** — selectively suppresses ptrace traps and minimizes context switches (in-process syscall interception, dramatically reduces overhead)

### 4.2 How Determinism Is Achieved

#### Single-Core Execution

rr runs **all threads on a single CPU core**, context-switching them sequentially. This eliminates:
- Data races (threads cannot touch shared memory concurrently)
- Weak memory model effects (everything is sequentially consistent on one core)
- Scheduling non-determinism (rr controls the schedule)

#### Hardware Performance Counters

This is the key enabler that makes rr practical:
- rr uses hardware performance counters to count **retired conditional branches (RCBs)** or instructions
- Asynchronous events (signals, context switches) are delivered at exactly the same point during replay by matching the counter value
- Counter accuracy is critical: if you execute 1,000,000 instructions from a given state during recording, you must reach the same state at 1,000,000 instructions during replay
- Requires Intel Nehalem (2010+), certain AMD Zen+, or certain AArch64 processors

#### What rr Records

- All inputs from the kernel to user-space processes
- Nondeterministic CPU effects (very few in practice)
- Scheduling decisions and counter values
- Syscall return values and data

Most instructions are deterministic. Nondeterminism is localized to relatively few points (typically I/O). rr only needs to record those points.

### 4.3 Replay Guarantees

Replay preserves:
- Instruction-level control flow
- All memory and register contents
- Memory layout (addresses of objects don't change)
- Syscall return data
- Register values at any point

### 4.4 Reverse Execution

rr extends gdb with efficient reverse-execution. Checkpoints use `fork()` (mostly copy-on-write) so memory overhead is minimal. To reach any point, rr rewinds to the nearest checkpoint and replays forward.

### 4.5 Performance

- **Firefox test suites:** ~1.2x slowdown (10 minutes becomes ~12 minutes)
- **Single-threaded programs:** much lower overhead than any competing record-and-replay system
- **Multi-threaded programs:** incur the slowdown of running on a single core (inherent limitation)
- **Chaos mode:** varies thread priorities between runs to find race condition bugs

### 4.6 Trade-offs

| Advantage | Disadvantage |
|-----------|-------------|
| Works with unmodified binaries on stock Linux kernel | Single-core only — parallel programs are slow |
| Very low overhead (~1.2x) for single-threaded workloads | Memory-ordering bugs (weak model) cannot be observed |
| Perfect replay with reverse-execution debugging | Requires specific Intel/AMD/ARM CPU models |
| No system configuration changes needed | Cannot record processes sharing memory with external processes |
| Lightweight recording (no snapshots) | Recording, not simulation — cannot inject faults or explore paths |

**Key sources:**
- [rr Project Website](https://rr-project.org/)
- [To Catch a Failure: The Record-and-Replay Approach (ACM Queue)](https://queue.acm.org/detail.cfm?id=3391621)
- [Deterministic Record-and-Replay (ACM Queue)](https://queue.acm.org/detail.cfm?id=3688088)
- [rr GitHub](https://github.com/rr-debugger/rr)

---

## 5. Hermit (Meta)

**What it is:** A deterministic Linux execution container developed at Meta (Facebook). Forces deterministic execution of arbitrary x86-64 Linux programs. Currently in **maintenance mode** (no longer actively developed).

**Repository:** [github.com/facebookexperimental/hermit](https://github.com/facebookexperimental/hermit)

### 5.1 Interception Mechanism

Built on **Reverie**, Meta's syscall interception framework ([github.com/facebookexperimental/reverie](https://github.com/facebookexperimental/reverie)):

- **Backend:** `reverie-ptrace` — uses `ptrace` to intercept all syscalls
- **Can also intercept:** `CPUID` and `RDTSC` instructions
- **Tool model:** Each Reverie tool is a set of callbacks invoked on trappable events (syscalls, signals). Tools subscribe to specific event streams to minimize overhead.

### 5.2 How Determinism Is Achieved

**Syscall handling — two strategies:**
1. **Complete replacement**: suppress the syscall entirely, provide synthetic deterministic response
2. **Sanitized forwarding**: allow syscall to execute but modify kernel response for determinism

**Thread scheduling:**
- Serializes all thread execution (eliminates actual CPU parallelism)
- Deterministically picks which thread runs next
- Uses CPU **Performance Monitoring Unit (PMU)** to stop execution after a fixed number of **retired conditional branches (RCBs)** — same technique as rr
- After N RCBs, the current thread is preempted and the next scheduled thread runs

**Specific sources of non-determinism handled:**
- **Randomness:** Intercepts `/dev/urandom` reads, substitutes deterministic PRNG with fixed seed
- **Time:** Controls virtual time passage through configuration
- **Thread scheduling:** PMU-based deterministic scheduling as described above

### 5.3 Limitations

- Does NOT isolate from: filesystem changes, external network responses
- "Long tail of unsupported system calls that may cause your program to fail"
- ptrace overhead is significant for syscall-heavy workloads
- No longer actively developed (maintenance mode)

### 5.4 Performance

- Slowdown depends on syscall frequency
- ptrace adds "significant overhead" for syscall-heavy workloads
- Can reduce overhead by subscribing only to required syscalls
- Future plans (never realized) included a more performant backend with near-function-call overhead

### 5.5 Trade-offs

| Advantage | Disadvantage |
|-----------|-------------|
| Works with unmodified binaries | High overhead for syscall-heavy workloads (ptrace) |
| Deterministic thread scheduling via PMU | Maintenance mode — no active development |
| Handles randomness, time, and scheduling | Long tail of unsupported syscalls |
| Open source (Rust) | Cannot isolate from filesystem/network changes |
| Reverie framework is reusable | Single-core execution only |

**Key sources:**
- [Hermit GitHub](https://github.com/facebookexperimental/hermit)
- [Reverie GitHub](https://github.com/facebookexperimental/reverie)
- [Deterministic Linux for controlled testing (HN discussion)](https://news.ycombinator.com/item?id=33708867)

---

## 6. MadSim

**What it is:** An open-source Magical Deterministic Simulator for distributed systems in Rust. Inspired by FoundationDB's approach but designed for the Rust/Tokio ecosystem. Replaces tokio and tonic with simulation-aware implementations at compile time.

**Repository:** [github.com/madsim-rs/madsim](https://github.com/madsim-rs/madsim)

### 6.1 How It Works

**Core approach:** Swap the async runtime at compile time.

- When built normally: crates are identical to original tokio/tonic
- When `madsim` config is enabled: tokio/tonic are replaced with simulation world
- All futures execute deterministically on a single thread given a seed

**Patched crates:** `quanta`, `getrandom`, `tokio-retry`, `tokio-postgres`, `tokio-stream`, and others. These are drop-in replacements that become simulation-aware under the `madsim` feature flag.

**Libc symbol overrides:** MadSim overrides libc symbols to control time and entropy (e.g., Rust's `HashMap` randomization for DoS prevention is made deterministic).

### 6.2 What Is Virtualized

| Component | Mechanism |
|-----------|-----------|
| **Network** | Simulated async network endpoint with controlled simulator |
| **File system** | Async file system simulation |
| **Time** | Deterministic time advancement (no real sleeps) |
| **Randomness** | Seeded PRNG from single seed |
| **Task scheduling** | Single-threaded deterministic executor |

### 6.3 Fault Injection API

MadSim provides APIs to:
- Kill processes
- Disconnect network links
- Inject I/O failures
- **BUGGIFY** module (inspired by FoundationDB) for cooperative failure injection

### 6.4 Performance

- Discrete-event simulation: no time wasted on sleep — full tests complete in seconds
- Single-threaded execution eliminates scheduling overhead
- No specific benchmark numbers published

### 6.5 Users

- **RisingWave** (distributed streaming SQL database)
- **S2** (distributed data system)
- **MadRaft** (Raft consensus labs)

### 6.6 Trade-offs

| Advantage | Disadvantage |
|-----------|-------------|
| Drop-in replacement for tokio-based Rust code | Rust-only (tokio ecosystem) |
| Deterministic from single seed | Must use patched crate versions |
| No sleep overhead (discrete-event) | Cannot test non-Rust components |
| Open source | Controlling third-party dependency randomness is difficult |
| BUGGIFY-style fault injection | Single-threaded: no true concurrency bug detection |

**Key sources:**
- [MadSim GitHub](https://github.com/madsim-rs/madsim)
- [MadSim Docs](https://docs.rs/madsim/latest/madsim/)
- [Applying Deterministic Simulation: The RisingWave Story](https://risingwave.com/blog/applying-deterministic-simulation-the-risingwave-story-part-2-of-2/)
- [Deterministic simulation testing for async Rust (S2)](https://s2.dev/blog/dst)

---

## 7. TigerBeetle

**What it is:** A high-performance financial transactions database written in Zig. Uses Zig's `comptime` (compile-time generics) to swap real I/O with simulated I/O, enabling deterministic simulation testing via the VOPR (Viewstamped Operation Replayer).

**Repository:** [github.com/tigerbeetle/tigerbeetle](https://github.com/tigerbeetle/tigerbeetle)

### 7.1 Zig Compile-Time Interface Swapping

Zig's `comptime` feature is the key enabler:

```
ReplicaType(Storage, Network, Time)
  |-- Production: io_uring (Linux) / IOCP (Windows), real TCP, wall clock
  |-- Simulation: in-memory storage, simulated network, virtual time
```

The `Storage`, `Network`, and `Time` types are generic parameters resolved at compile time. Production and simulation share the exact same code path — only the I/O backing differs.

### 7.2 What Is Virtualized

| Interface | Production | Simulation |
|-----------|-----------|------------|
| **Storage** | Direct I/O via `io_uring` (Linux) / IOCP (Windows) | In-memory storage with fault injection |
| **Network** | Real TCP | Per-path packet queues with configurable capacity |
| **Time** | Wall clock | `TimeSim` with controlled `tick_ms` increments and per-replica clock offsets |

### 7.3 VOPR Fault Injection

**Network faults:**
- Packet loss (probabilistic)
- Packet replay/duplication
- Network partitions (single node isolation, bilateral split, symmetric/asymmetric)
- One-way delay modeling with configurable latency distributions
- Path clogging (temporary congestion)

**Storage faults:**
- Read/write fault probability
- Read/write latency simulation

**Replica faults:**
- Random crashes and restarts with configurable probability
- Minimum stability periods between state changes

### 7.4 State Verification

Multiple continuous checkers run during simulation:
- **StateChecker**: validates linearizability, consistent total order of committed operations
- **StorageChecker**: superblock consistency, free set correctness, manifest integrity
- **ManifestChecker**: LSM structure, table visibility, compaction invariants
- **GridChecker**: block write tracking, checksums, address validation, corruption detection

Plus 6,000+ assertions compiled into the production binary.

### 7.5 Scale of Testing

- **1,000 dedicated CPU cores** running 24/7/365
- **2 millennia of simulated runtime per day**
- 3.3 seconds of VOPR simulation = 39 minutes of real-world testing time
- VOPR receives 8x weight vs. other fuzzers in continuous fuzzing orchestrator

### 7.6 Testing Modes

| Mode | Replicas | Clients | Requests |
|------|----------|---------|----------|
| Lite | 3 | 2 | 1,000 |
| Default (swarm) | 3-6 | 2-100 | 1,000,000 |
| Performance | 6 | 8-32 | 10,000,000 |

### 7.7 Trade-offs

| Advantage | Disadvantage |
|-----------|-------------|
| Zero-abstraction cost via Zig comptime | Zig-only (language-specific) |
| Same binary in production and simulation | Must design system around simulation from day one |
| Massive scale: 2 millennia simulated per day | Single-threaded simulation |
| Multiple continuous state checkers | Cannot test arbitrary binaries |
| Deterministic replay from seed | Fault injection quality depends on understanding real failure modes |

**Key sources:**
- [TigerBeetle Safety Docs](https://docs.tigerbeetle.com/concepts/safety/)
- [TigerBeetle Testing and Simulation (DeepWiki)](https://deepwiki.com/tigerbeetle/tigerbeetle/5.2-testing-and-simulation)
- [A Descent Into the Vortex](https://tigerbeetle.com/blog/2025-02-13-a-descent-into-the-vortex/)
- [TigerBeetle Architecture](https://github.com/tigerbeetle/tigerbeetle/blob/main/docs/internals/ARCHITECTURE.md)

---

## 8. Comparison Matrix

### Interception Mechanism

| System | Mechanism | Level | Code Changes Required |
|--------|-----------|-------|-----------------------|
| **Antithesis** | Custom hypervisor (modified bhyve + VMX) | Hardware/VM | None (any containerized binary) |
| **Shadow** | LD_PRELOAD + seccomp-bpf | Syscall (user-space) | None (any dynamically linked binary) |
| **FoundationDB Sim2** | Interface polymorphism (`g_network` pointer swap) | Application (compile-time) | Must use Flow language |
| **rr** | ptrace + seccomp-bpf + HW perf counters | Syscall (user-space) | None (any binary) |
| **Hermit** | ptrace via Reverie + PMU | Syscall (user-space) | None (any binary) |
| **MadSim** | Tokio runtime swap via feature flag | Application (compile-time) | Must use patched crates |
| **TigerBeetle** | Zig comptime generic swap | Application (compile-time) | Must use Zig comptime generics |

### What They Intercept

| System | Network | Disk | Time | Randomness | Threads | Signals |
|--------|---------|------|------|------------|---------|---------|
| **Antithesis** | Yes (all VM I/O) | Yes | Yes (TSC, HPET, all timers) | Yes | Yes (single vCPU) | Yes |
| **Shadow** | Yes (full TCP/UDP sim) | Partial (file descriptors) | Yes (discrete event) | Yes (seeded PRNG) | Yes (controlled) | Yes |
| **FDB Sim2** | Yes (INetwork/IConnection) | Yes (IAsyncFile) | Yes (simulated clock) | Yes (deterministicRandom) | N/A (single-threaded) | N/A |
| **rr** | Record only | Record only | Record only | Record only | Yes (single core) | Yes (recorded) |
| **Hermit** | Partial | No (not isolated) | Yes | Yes (PRNG) | Yes (PMU-based) | Yes |
| **MadSim** | Yes (simulated) | Yes (simulated) | Yes (no real sleeps) | Yes (seeded) | N/A (single-threaded) | N/A |
| **TigerBeetle** | Yes (simulated queues) | Yes (in-memory + faults) | Yes (TimeSim) | Yes (seeded) | N/A (single-threaded) | N/A |

### Performance Overhead

| System | Overhead | Notes |
|--------|----------|-------|
| **Antithesis** | Not publicly quantified | Prioritizes throughput over latency; faster-than-real-time for sleeps |
| **Shadow** | Varies by workload | LD_PRELOAD fast path minimizes overhead; CPU modeled as infinite |
| **FDB Sim2** | Near-zero (in-process) | Years of uptime in seconds; all in-memory |
| **rr** | ~1.2x single-threaded (Firefox) | Multi-threaded programs incur single-core slowdown |
| **Hermit** | Significant for syscall-heavy | ptrace overhead dominates |
| **MadSim** | Near-zero (in-process) | Discrete-event, no real sleeps |
| **TigerBeetle** | Near-zero (in-process) | 3.3s simulation = 39 min real time |

### Architecture Classification

| Approach | Systems | Pros | Cons |
|----------|---------|------|------|
| **Hypervisor-level** | Antithesis | Works with anything; complete isolation | Complex; expensive; single-core per VM |
| **Syscall interception** | Shadow, Hermit, rr | Works with unmodified binaries | Overhead from ptrace/seccomp; incomplete syscall coverage |
| **Application-level (runtime swap)** | FDB Sim2, MadSim, TigerBeetle | Fastest; most complete control | Language-specific; must design for simulation from the start |

---

[← Back](README.md)
