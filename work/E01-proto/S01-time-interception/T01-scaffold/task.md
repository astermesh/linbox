# T01: Project Scaffold

**Story:** [S01: Time Interception](../story.md)
**Status:** Backlog

## Description

Set up the C project infrastructure for liblinbox.so development on Linux.

## Deliverables

- Directory structure: `src/shim/`, `src/controller/`, `src/tests/`
- `CMakeLists.txt` — builds `liblinbox.so` (shared library, -fPIC, C11), links `-ldl`, supports Debug/Release/ASAN build types
- Top-level `Makefile` — convenience wrapper: `make build`, `make debug`, `make release`, `make test`, `make clean`, `make fmt`, `make lint`
- `.clang-format` — C code style (4-space indent, 100 char width)
- `.gitignore` — add C build artifacts (`build/`, `*.o`, `*.so`, `*.a`, `*.d`)
- `scripts/setup-linux.sh` — installs all required packages on Ubuntu/Debian (idempotent)
- Empty `liblinbox.so` that compiles and loads without errors

## Tests

- `make build` succeeds, produces `build/liblinbox.so`
- `make test` runs (empty test suite, exit 0)
- `LD_PRELOAD=./build/liblinbox.so /bin/true` exits 0 (shim loads without crashing)
- `make fmt` runs clang-format without errors
- `make clean` removes build directory

---

[← Back](../story.md)
