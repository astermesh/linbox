# ADR-012: Docker as Container Runtime

**Status:** Accepted
**Date:** 2025-05
**Context:** E01-S06 design

## Decision

Use Docker (with docker-compose) as the container runtime for the proto phase.

## Rationale

- **Ubiquitous:** Docker is the most widely available container runtime, lowest barrier to entry for developers
- **docker-compose:** Simple multi-container orchestration (controller + sandbox containers)
- **Seccomp profile support:** Docker natively supports custom seccomp JSON profiles, making io_uring blocking straightforward
- **Volume mounts:** Unix socket sharing between controller and sandbox via shared volumes
- **LD_PRELOAD injection:** Set via Dockerfile entrypoint or environment variables

## Alternatives Considered

- **Podman** — rootless by default, compatible with Docker CLI. Good alternative but less common in development. Can be supported later with minimal changes.
- **containerd directly** — lower level, more control, but more setup for developers.
- **Custom namespace setup (unshare/nsenter)** — maximum control but no ecosystem tooling. Too much infrastructure work for proto.

## Consequences

- Docker and docker-compose are development dependencies
- Container images are defined via Dockerfiles
- Seccomp profile is a JSON file passed to Docker
- Podman compatibility is likely but not guaranteed — test in Phase 2

---

[← Back to ADRs](README.md)
