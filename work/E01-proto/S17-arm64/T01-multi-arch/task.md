# T01: ARM64 Seccomp + Signal Handling

**Story:** [S17: ARM64 Support](../story.md)
**Status:** Backlog

## Description

Port architecture-dependent code to ARM64. seccomp-bpf filter must use ARM64 syscall numbers. SIGSYS handler must extract arguments from ARM64 registers (x0-x5) and set return value in x0. Build system must support cross-compilation.

## Deliverables

- `src/shim/arch/` — architecture abstraction layer:
  - `src/shim/arch/arch.h` — common interface: `ARCH_SYSCALL_NR(ctx)`, `ARCH_ARG0(ctx)`, ..., `ARCH_SET_RETURN(ctx, val)`
  - `src/shim/arch/x86_64.h` — x86-64 implementation (existing code extracted here): REG_RAX for syscall nr, REG_RDI/RSI/RDX/R10/R8/R9 for args
  - `src/shim/arch/aarch64.h` — ARM64 implementation: x8 for syscall nr, x0-x5 for args, x0 for return value
- Update `src/shim/seccomp.c` — use architecture-specific syscall numbers:
  - `__NR_clock_gettime` is 113 on ARM64 vs 228 on x86-64
  - BPF filter must check `seccomp_data.arch` (AUDIT_ARCH_AARCH64 vs AUDIT_ARCH_X86_64)
  - Reject unexpected architectures (prevent syscall number confusion attacks)
- Update `src/shim/sigsys.c` — use `ARCH_SYSCALL_NR(ctx)`, `ARCH_ARGn(ctx)`, `ARCH_SET_RETURN(ctx, val)` macros
- Update `CMakeLists.txt`:
  - Detect target architecture at build time
  - Cross-compilation toolchain file for ARM64: `cmake -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.cmake`
  - `cmake/aarch64-linux-gnu.cmake` — set cross-compiler, sysroot
- `docker/Dockerfile.arm64` — multi-arch build (buildx)

## Tests

- On ARM64: `make build` → produces `liblinbox.so` for aarch64
- On ARM64: `LD_PRELOAD=liblinbox.so date` → shows fake date
- On ARM64: direct `syscall(__NR_clock_gettime, ...)` → SIGSYS handler returns fake time
- On ARM64: seccomp filter uses correct syscall numbers (113 for clock_gettime)
- On x86-64: cross-compile for ARM64 → produces aarch64 .so (file confirms ELF aarch64)
- Architecture detection: build on x86-64 → x86-64 filter. Build on ARM64 → ARM64 filter.
- Architecture validation in BPF filter: x86-64 process sending ARM64 syscall numbers → rejected

---

[← Back](../story.md)
