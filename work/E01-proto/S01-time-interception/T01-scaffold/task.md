# T01: Project Scaffold

**Story:** [S01: Time Interception](../story.md)
**Status:** Backlog

## Description

Set up the C project infrastructure for liblinbox.so development on Linux.

## Required Reading

Before starting, read these docs to understand the overall architecture:

- [Project structure](../../../docs/project-structure.md) — code layout, components, how they connect
- [Simbox architecture](../../../docs/architecture.md) — what LinBox is and how it fits into simbox
- [Development environment](../../../docs/env.md) — required packages
- [ADR-009: C language for shim](../../../docs/adr/009-c-language-shim.md) — why C, not C++/Rust

## Deliverables

- Directory structure: `src/shim/`, `src/controller/`, `src/common/`, `src/proxy/`, `src/ebpf/`
- `CMakeLists.txt` — builds `liblinbox.so` (shared library, -fPIC, C11), links `-ldl`, supports Debug/Release/ASAN build types
- Top-level `Makefile` — convenience wrapper: `make build`, `make debug`, `make release`, `make test`, `make clean`, `make fmt`, `make lint`
- `.clang-format` — C code style (4-space indent, 100 char width)
- `.gitignore` — add C build artifacts (`build/`, `*.o`, `*.so`, `*.a`, `*.d`)
- `scripts/setup-linux.sh` — installs all required packages on Ubuntu/Debian (idempotent)
- Empty `liblinbox.so` that compiles and loads without errors
- Test infrastructure (see [testing strategy](../../../rnd/testing/strategy.md)):
  - Criterion test framework via CMake FetchContent
  - CMake helper functions: `linbox_add_unit_test(NAME SOURCE)` for `*.test.c` (Criterion, no LD_PRELOAD), `linbox_add_preload_test(NAME SOURCE)` for `*.preload.c` (standalone binary, CTest runs with `LD_PRELOAD=$<TARGET_FILE:linbox>`)
  - File naming convention: `foo.test.c` = unit test (logic only), `foo.preload.c` = integration test (real LD_PRELOAD interception)
  - `make test` runs all unit + preload tests via CTest

## Tests

- `make build` succeeds, produces `build/liblinbox.so`
- `make test` runs CTest (empty test suite, exit 0)
- `LD_PRELOAD=./build/liblinbox.so /bin/true` exits 0 (shim loads without crashing)
- `make fmt` runs clang-format without errors
- `make clean` removes build directory
- Criterion is fetched and builds successfully
- A trivial `src/shim/linbox.test.c` (single `cr_assert(1)` test) compiles and passes via `make test`

---

[← Back](../story.md)
