# ADR-007: Linux Namespaces for Isolation

**Status:** Accepted
**Date:** 2025-05
**Context:** R01-boxing research

## Decision

Use Linux namespaces (PID, network, mount, user, UTS, IPC) as the foundational isolation layer (Layer 1). These are provided "for free" by container runtimes.

## Context

LinBox needs process, network, and filesystem isolation. Linux namespaces provide this at the kernel level with zero CPU overhead.

## Rationale

| Namespace | Purpose in LinBox | Overhead |
|-----------|-------------------|----------|
| **PID** | Virtual PIDs, process tree isolation | Zero |
| **Network** | Isolated network stack, veth pairs for controlled connectivity | Zero |
| **Mount** | Overlay filesystem, isolated `/dev/urandom` | Zero |
| **User** | UID/GID mapping for rootless operation | Zero |
| **UTS** | Hostname isolation per box | Zero |
| **IPC** | SysV IPC isolation | Zero |
| **Time** (Linux 5.6+) | CLOCK_MONOTONIC/BOOTTIME offset (no CLOCK_REALTIME) | Zero |

Time namespace is useful but insufficient alone — it doesn't support CLOCK_REALTIME and cannot control clock speed. LD_PRELOAD is still needed for full time control (ADR-001).

## Consequences

- Container runtime (Docker/Podman) handles namespace creation
- No additional implementation needed for basic isolation
- Network namespace combined with veth pairs enables controlled network topology
- Mount namespace enables overlay fs for filesystem snapshots

---

[← Back to ADRs](README.md)
