# Simbox Architecture

This document describes the Simbox simulation framework and how LinBox fits into it. Intended as a quick reference for anyone working in this repository.

## Core Concepts

### Simbox

Simbox is the simulation runtime and conceptual framework. It defines four core abstractions — Lab, Box, Sim, and Law — and provides the hookable boundary model that all service emulators are built on.

### Box

A Box is a service emulator that wraps a real execution engine (Eng) with fully hookable boundaries. It has real state (tables, rows, files, keys) and speaks the service's standard protocol.

Every Box exposes three interfaces:

- **IBI (Inbound Boundary Interface)** — entry points where external calls arrive (queries, HTTP requests, Redis commands)
- **OBI (Outbound Boundary Interface)** — where the engine calls outward (time, network, filesystem, randomness)
- **CBI (Control Boundary Interface)** — direct owner access that bypasses hooks (for Lab/Sim internal use)

```
        ┌─────────────────────┐
   IBI  │                     │  OBI
   ────►│   Eng (Engine)      ├────►
        │   (real state)      │
        └─────────┬───────────┘
                  │
                  ▼ CBI
            (control access)
```

**Parity rule:** with no hooks registered, a Box must behave identically to the real service. Any difference is a bug.

Examples: PgBox (PostgreSQL via PGlite), NodeBox (Node.js runtime), RedisBox (Redis in-memory store).

### Sim

Sim is per-Box behavior simulation. It knows the domain and injects realism that the Box alone cannot provide.

| Aspect   | Box                         | Sim                                  |
|----------|-----------------------------|--------------------------------------|
| What     | Engine + real state         | Behavior model + virtual state       |
| Example  | 100 rows in PGlite         | Declares 1M rows virtually           |
| Role     | Execute operations          | Calculate realistic effects          |

Sim hooks into Box boundaries (IBI/OBI) and can delay, modify, or fail any operation. Example: a query takes 1ms in PgBox, but PgSim says "with 1M rows this takes 2s" — Sim wins.

Examples: PgSim (paired with PgBox), NodeSim (paired with NodeBox).

### Law

Law is an inter-Sim interaction rule. It activates only when matching Sims are present in the Lab.

- **TLaw** — virtual time: synchronized clock for all boxes
- **MLaw** — machine resources: CPU contention, memory pressure
- **NetLaw** — network effects: latency, packet loss, partitions

Laws coordinate Sims without direct Sim-to-Sim communication. Example: "PG load → CPU contention → Node slowdown" is NetLaw + MLaw, not PgSim talking to NodeSim.

### Lab

Lab is the simulation environment — a pure container where Boxes, Sims, and Laws are assembled and auto-wired. It has no behavior of its own.

```
Lab
 ├── PgSim → uses PgBox
 ├── NodeSim → uses NodeBox
 ├── TLaw (virtual time)
 ├── MLaw (machine resources)
 └── NetLaw (network)
```

## Hook Protocol (SBP)

Every boundary point uses the SimBox Protocol hook pattern:

```
pre → optional next() → post → final resolution
```

Two hook types:

- **AsyncHook** `(ctx, next) => Promise<T>` — for IBI and async OBI; can delay, modify, or short-circuit
- **SyncHook** `(next) => T` — for WASM boundaries (time, random); must return immediately

## Boxification

Boxification is the process of wrapping a service with hookable boundaries. Steps:

1. **Select Eng** — what executes operations (PGlite, Node.js runtime, Redis binary)
2. **Map IBI** — where do external calls enter (PG wire protocol, HTTP, RESP)
3. **Map OBI** — where does the engine call outward (time, network, fs, random)
4. **Apply hooks** — wrap every boundary with the hook protocol
5. **Verify parity** — confirm that Box without Sim behaves identically to the real service

Boxing depths:

| Depth   | Method               | IBI coverage | OBI coverage |
|---------|----------------------|--------------|--------------|
| Full    | In-process (WASM)    | All          | All          |
| Partial | Container + shim     | Network      | Some         |
| Minimal | Cloud/external       | Network only | None         |

## Where LinBox Fits

LinBox extends boxification to **real native services** (PostgreSQL, Redis, Nginx, Node.js) running as unmodified Linux processes.

In-process boxes (PgBox, NodeBox) achieve full-depth boxing through WASM or JS module shimming. But production services are native binaries — they call the kernel directly. LinBox solves this with a layered interception architecture:

| Layer | Mechanism                    | Purpose                     | Overhead    |
|-------|------------------------------|-----------------------------|-------------|
| 0     | Container (Docker/Podman)    | Isolation                   | ~0          |
| 1     | Namespaces + cgroups         | PID, network, mount, timens | ~0          |
| 2     | tc/netem + iptables          | Network condition simulation| ~0          |
| 3     | LD_PRELOAD + seccomp         | Syscall interception        | ~100ns–800ns|
| 4     | TPROXY / eBPF                | Protocol-level interception | varies      |

**Primary mechanism:** an `LD_PRELOAD` shim (`liblinbox.so`) that intercepts libc calls at ~100ns overhead per call.

**Safety net:** seccomp-bpf filter catches direct syscalls (bypassing libc) via SIGSYS handler at ~800ns overhead.

**Communication:** the shim talks to an external controller via shared memory (hot path) and unix socket (control path) using a custom binary protocol (SBP wire format).

### OBI Interception Points (~94 total)

| Category    | Examples                                         | Count |
|-------------|--------------------------------------------------|-------|
| Time        | clock_gettime, gettimeofday, nanosleep, timerfd  | ~20   |
| Randomness  | getrandom, /dev/urandom, /dev/random             | ~6    |
| Network     | socket, connect, bind, send, recv, DNS           | ~20   |
| Filesystem  | open, read, write, stat, mmap                    | ~30   |
| Process     | fork, clone, execve, getpid, signals             | ~18   |

### Relationship to Other Boxes

```
simbox (framework)
  ├─ PgBox      full-depth, in-process (WASM)
  ├─ NodeBox    full-depth, in-process (JS shim)
  ├─ RedisBox   full-depth, in-process (JS/binary)
  └─ LinBox     partial-depth, container + LD_PRELOAD
       └─ boxes real PostgreSQL, Redis, Nginx, Node.js
```

LinBox trades boxing depth for universality: any service that runs on Linux can be boxed without modification, at the cost of slightly less granular OBI coverage compared to in-process boxes.

---

[← Back](README.md)
