# R01: Boxing

Research into building a fully hookable Linux sandbox — interception mechanisms, deterministic systems, architecture.

## Status

Complete — comprehensive analysis across 5 research topics.

## Output

All research output in [rnd/boxing/](../../rnd/boxing/README.md):

- [LD_PRELOAD Interception](../../rnd/boxing/ld-preload.md) — dynamic linker resolution, interceptable libc functions, code patterns, limitations, IPC transport
- [Sandbox Alternatives](../../rnd/boxing/sandbox-alternatives.md) — gVisor, FUSE, namespaces, timens, seccomp notifier, network interception, eBPF, unikernels
- [Seccomp-BPF and Ptrace](../../rnd/boxing/seccomp-ptrace.md) — kernel interception mechanisms, seccomp_unotify, LD_PRELOAD hybrid
- [Deterministic Systems](../../rnd/boxing/deterministic-systems.md) — Antithesis, Shadow, FDB Sim2, rr, Hermit, MadSim, TigerBeetle
- [LinBox Architecture](../../rnd/boxing/linux-sandbox.md) — layered architecture, ~94 interception points, cost estimation, roadmap

## Key Finding

The **LD_PRELOAD + seccomp notifier dual-layer** strategy provides universal syscall coverage:
- LD_PRELOAD handles the common case (libc-using programs) at ~100ns per call
- Seccomp notifier catches everything LD_PRELOAD misses (Go, static binaries, direct syscalls) at ~2-5us per call
- Non-intercepted syscalls pass through at native speed

## Recommended Layered Architecture

1. **Layer 0**: Container runtime (Docker/Podman)
2. **Layer 1**: Namespaces (PID, network, mount, timens) + cgroups — zero overhead isolation
3. **Layer 2**: tc/netem + iptables — network condition simulation
4. **Layer 3**: LD_PRELOAD (primary) + seccomp notifier (fallback) — syscall interception
5. **Layer 4**: TPROXY/eBPF — wire protocol interception

---

[← Back](../README.md)
