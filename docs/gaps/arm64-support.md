# Gap: ARM64 Architecture Support

**Severity:** Low
**Blocks:** Nothing (Phase 4)

## What's Missing

Current design and implementation target x86-64 only:

- seccomp-BPF filter references x86-64 syscall numbers and register names
- SIGSYS handler accesses `ucontext_t->uc_mcontext.gregs` which is architecture-specific
- Signal context register mapping (`REG_RAX`, `REG_RDI`, etc.) differs on ARM64
- seqlock memory ordering may need different barriers on ARM64 (weaker memory model)

## What's Decided

- ARM64 is explicitly Phase 4
- x86-64 is the only target for proto and Phase 2-3

## Resolution Path

When ARM64 support is needed:
1. Abstract syscall number tables behind architecture-specific headers
2. Abstract register access in SIGSYS handler
3. Test seqlock correctness on ARM64 (may need explicit `dmb` barriers)
4. CI pipeline with ARM64 runners (GitHub Actions supports ARM64)

---

[← Back to gaps](README.md)
