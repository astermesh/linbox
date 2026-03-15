# Gap: macOS Development Support

**Severity:** Medium
**Blocks:** Developer experience for macOS users

## What's Missing

LinBox targets Linux (namespaces, seccomp, LD_PRELOAD are Linux-specific). macOS developers need a development path:

- Cross-compilation from macOS to Linux (or development inside Linux VM)
- IDE integration with remote/VM development
- Test execution (must run on Linux — Docker on macOS? Lima? OrbStack?)
- `docs/env.md` lists macOS as "TBD"

## What's Known

- `scripts/setup-linux.sh` exists for Ubuntu/Debian
- All runtime components are Linux-only
- Development (editing, building shim) could potentially work on macOS with cross-compilation

## Resolution Path

Options:
1. **Docker-based dev environment** — Dockerfile with all dev tools, mount source code
2. **Lima/OrbStack** — lightweight Linux VM on macOS with automatic file sharing
3. **Remote dev (SSH + VSCode)** — develop on a Linux server/VM

Pick one and document in `docs/env.md`.

---

[← Back to gaps](README.md)
