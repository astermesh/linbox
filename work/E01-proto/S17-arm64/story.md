# S17: ARM64 Support

**Epic:** Proto
**Status:** Backlog

## Business Result

LinBox runs on ARM64 (aarch64) architecture. Seccomp filters, SIGSYS handler register access, and all architecture-dependent code works on ARM64. This enables running LinBox on ARM servers (AWS Graviton, Apple Silicon VMs) and Raspberry Pi.

## Scope

- seccomp-bpf filter: ARM64 syscall numbers (different from x86-64)
- SIGSYS handler: ARM64 register mapping (x0-x5 for args, x8 for syscall number, x0 for return)
- Architecture detection at build time (conditional compilation)
- Cross-compilation support (build ARM64 shim on x86-64)
- Test on ARM64 (CI or manual)

## Tasks

- [T01: ARM64 seccomp + signal handling](T01-multi-arch/task.md)

## Dependencies

- S01 (shim scaffold — base code to extend)
- S04 (seccomp — architecture-dependent filter and handler)

## Acceptance Criteria

- `liblinbox.so` builds and loads on ARM64 Linux
- seccomp filter uses correct ARM64 syscall numbers
- SIGSYS handler correctly extracts/sets ARM64 registers
- All S01-S04 tests pass on ARM64
- Cross-compilation from x86-64 to ARM64 works

---

[← Back](../epic.md)
