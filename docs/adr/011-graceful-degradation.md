# ADR-011: Graceful Degradation

**Status:** Accepted
**Date:** 2025-05
**Context:** E01-S02 design

## Decision

If the controller is unavailable (socket connect fails, controller dies mid-session), the shim falls back to real time and logs a warning. It does not crash the target process.

## Rationale

- **Development experience:** Developers may load liblinbox.so without a controller running (e.g., debugging, testing the shim in isolation). Crashing would be hostile.
- **Resilience:** If the controller crashes during a simulation, it's better to degrade visibly than to kill all sandbox processes.
- **Debuggability:** Warning messages on stderr help diagnose configuration issues.

## Alternatives Considered

- **Abort on missing controller** — strict but hostile to developers. Makes the shim impossible to test in isolation.
- **Block until controller available** — hangs the target process, potentially forever.
- **Use default virtual time (epoch)** — confusing. Real time is a better fallback because it's obviously "not controlled."

## Consequences

- Shim must check controller connection status on every control-plane operation
- Time reads from shared memory must handle the "no shared memory mapped" case
- Fallback mode is clearly logged so it's never silently degraded

---

[← Back to ADRs](README.md)
