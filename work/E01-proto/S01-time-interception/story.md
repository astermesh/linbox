# S01: Time Interception

**Epic:** Proto
**Status:** Backlog

## Required Reading

- [Project structure](../../docs/project-structure.md)
- [Simbox architecture](../../docs/architecture.md)
- [LD_PRELOAD interception](../../rnd/boxing/ld-preload.md)
- [ADR-009: C language](../../docs/adr/009-c-language-shim.md)

## Business Result

A process loaded with `liblinbox.so` via `LD_PRELOAD` believes it is a different time. All time-related libc calls return a controlled fake value.

## Scope

- Project scaffold (build system, directory structure, dev tooling)
- LD_PRELOAD shim with `__attribute__((constructor))` initialization
- Interception of `clock_gettime`, `gettimeofday`, `time`
- Hardcoded fake time (no controller yet)

## Tasks

- [T01: Project scaffold](T01-scaffold/task.md)
- [T02: Time interception](T02-time-intercept/task.md)

## Dependencies

None — this is the first story.

## Acceptance Criteria

- `make build` produces `liblinbox.so`
- `make test` passes all time interception tests
- `LD_PRELOAD=./build/liblinbox.so date` shows fake date

---

[← Back](../epic.md)
