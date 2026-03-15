# Gap: RDTSC and Hardware Instructions

**Severity:** Low (accepted leakage)
**Blocks:** Nothing

## What's Missing

Some CPU instructions read time or generate randomness without making syscalls:

| Instruction | Purpose | Interceptable? |
|-------------|---------|---------------|
| `RDTSC` / `RDTSCP` | Read Time Stamp Counter | Only in VM (trap via VMX) |
| `RDRAND` | Hardware random number | Can disable via CPUID masking |
| `RDSEED` | Hardware random seed | Can disable via CPUID masking |

## What's Decided

- **RDTSC leakage is accepted** — most applications don't use RDTSC directly. Those that do (JIT compilers, performance profiling) will see real hardware time. This is documented as a known limitation.
- **RDRAND/RDSEED** — Shadow disables these via CPUID masking (making the CPU appear to not support them, so applications fall back to `getrandom`). LinBox should do the same.

## Resolution Path

- CPUID masking for RDRAND/RDSEED: implement in S03 (Random Interception)
- RDTSC: accept leakage unless running inside a VM where it can be trapped

---

[← Back to gaps](README.md)
