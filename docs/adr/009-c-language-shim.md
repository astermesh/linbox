# ADR-009: C for the Shim Library

**Status:** Accepted
**Date:** 2025-05
**Context:** E01-S01 design

## Decision

Implement `liblinbox.so` (the LD_PRELOAD shim) in C.

## Rationale

- **LD_PRELOAD requires a shared library** that interposes libc symbols. C is the natural language for this — same ABI, same calling convention, no runtime initialization overhead.
- **dlsym(RTLD_NEXT)** and libc function signatures are C APIs.
- **Minimal footprint:** No runtime, no garbage collector, no allocator overhead. The shim is loaded into every process.
- **seccomp-BPF and SIGSYS handlers** operate at the syscall/signal level — C maps directly to these primitives.
- **Proven pattern:** libfaketime, fakeroot, proxychains, Shadow's shim — all are C.

## Alternatives Considered

- **Rust** — safe, modern, but FFI overhead for libc interposition, heavier binary, runtime initialization. Better suited for the controller.
- **C++** — possible but adds unnecessary complexity (RAII, exceptions) for what is essentially a thin syscall wrapper.
- **Zig** — compelling (comptime, no hidden allocations) but smaller ecosystem and team familiarity.

## Consequences

- Must be careful with memory safety (no Rust safety net)
- Build system uses CMake
- Code style enforced via `.clang-format`
- Controller may use a different language in the future (Rust, Go) — only the shim must be C

---

[← Back to ADRs](README.md)
