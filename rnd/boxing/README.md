# Boxing

Research into building a fully hookable Linux sandbox — interception mechanisms, deterministic systems, architecture.

## Topics

- [LD_PRELOAD Interception](ld-preload.md) — dynamic linker resolution, interceptable libc functions, code patterns, limitations, IPC transport
- [Sandbox Alternatives](sandbox-alternatives.md) — gVisor, FUSE, namespaces, timens, seccomp notifier, network interception, eBPF, unikernels
- [Seccomp-BPF and Ptrace](seccomp-ptrace.md) — kernel interception mechanisms, seccomp_unotify, LD_PRELOAD hybrid
- [Deterministic Simulation Systems](deterministic-systems.md) — Antithesis, Shadow, FDB Sim2, rr, Hermit, MadSim, TigerBeetle
- [LinBox Architecture](linux-sandbox.md) — consolidated findings: layered architecture, ~94 interception points, cost estimation, roadmap

---

[← Back](../README.md)
