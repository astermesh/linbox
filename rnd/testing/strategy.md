# Testing Strategy for LinBox

Research on the best testing approach for a C11 project that builds an LD_PRELOAD shared library (liblinbox.so) and a controller process.

## 1. C Test Framework Comparison

### Evaluated Frameworks

| Framework | Language | Auto-discovery | Fork isolation | Signal testing | CMake integration | Output formats | Maturity |
|-----------|----------|---------------|----------------|----------------|-------------------|----------------|----------|
| **CMocka** | C | No (manual array) | No (single process) | No | find_package, FetchContent | TAP, xUnit XML, Subunit | High (Samba, libssh, BIND) |
| **Criterion** | C/C++ | Yes (automatic) | Yes (per-test process) | Yes (.signal = SIGSYS) | find_package, FetchContent | TAP, xUnit XML, JSON | Medium-High |
| **Check** | C | No (manual registration) | Yes (fork per test) | Yes (signal testing) | find_package, pkg-config | TAP, XML, Subunit | High (long-standing) |
| **Unity** | C | No (manual) | No | No | CMakeLists included | Custom | High (embedded focus) |
| **GoogleTest** | C++ (extern "C") | Yes (automatic) | No (single process) | Death tests | FetchContent, find_package | xUnit XML | Very High |
| **Plain assert** | C | N/A | N/A | N/A | N/A | Exit code only | N/A |

### Analysis

**CMocka** (v2.0.2, Jan 2026) — Best for pure unit testing of C code. Excellent mock support via `will_return()` / `expect_*()` macros. Type-safe assertions (`assert_int_equal`, `assert_memory_equal`, `assert_return_code`). Supports TAP and xUnit XML for CI. Requires only the C standard library. Adopted by major system projects (Samba, libssh, OpenVPN, BIND). Weakness: no fork-based test isolation, so a crashing test kills the runner.

**Criterion** (v2.4.3, Oct 2024) — Best overall for this project. Each test runs in its own process (fork-based), so crashes and signals are caught. Built-in signal expectation (`.signal = SIGSYS` on a test). Automatic test discovery — no need to maintain a manual test array. TAP and xUnit XML output. Requires C99 (we use C11, so fine). Fixtures (`.init`, `.fini`) at test and suite level. The one downside: smaller community than CMocka.

**Check** (v0.15+) — Fork-based isolation similar to Criterion, but more verbose API (`START_TEST` / `END_TEST`, `Suite`, `TCase`, `SRunner`). Solid but feels heavyweight compared to Criterion.

**Unity** — Designed for embedded, not Linux system-level. No process isolation. Poor fit.

**GoogleTest** — Excellent framework but requires C++. Wrapping C code via `extern "C"` is possible but adds friction. Death tests (fork + expect crash) are powerful. Best if the project were C++.

**Plain assert + custom runner** — Zero dependencies, full control, but you end up reimplementing test discovery, output formatting, and isolation. Good for quick ad-hoc tests, not for a growing test suite.

### Recommendation: Criterion as primary, CMocka for mock-heavy units

**Criterion** is the best fit for LinBox because:
1. **Fork-based isolation** — essential when testing code that installs signal handlers, seccomp filters, or may crash
2. **Signal testing** — `.signal = SIGSYS` directly tests the seccomp SIGSYS handler
3. **Automatic test discovery** — co-located test files (time.test.c) just need to be compiled, no manual registration
4. **Clean syntax** — `Test(suite, name) { cr_assert(...); }` is minimal boilerplate
5. **CMake-native** — works with FetchContent

Use **CMocka** selectively for unit tests that need mock injection (e.g., testing SBP message parsing with mocked socket reads).

## 2. How Other LD_PRELOAD Projects Test

### libfaketime

**Approach:** Custom shell-based test framework (`testframe.sh`) + C test programs.

- Test programs (`timetest.c`) are standalone binaries that call `time()`, `clock_gettime()`, `gettimeofday()` etc. and print results
- A shell harness (`test.sh`) runs each test program with `LD_PRELOAD=../src/libfaketime.so.1 FAKETIME="-15d" ./timetest`
- Results are checked by the shell script (output comparison or exit code)
- Functional tests in `functests/` directory
- Shared memory layout tests (`shm_layout_test.c`)
- No formal test framework — plain printf + manual verification

**Key insight:** libfaketime separates the "test subject" (a simple C program that calls time functions) from the "test harness" (shell script that sets LD_PRELOAD and checks output). This two-binary pattern is fundamental.

### Shadow Simulator

**Approach:** Rust test framework + C test programs run under Shadow.

- Tests organized by syscall category: `src/test/time/clock_gettime/`, `src/test/socket/`, etc.
- Each test is a standalone binary that exercises syscalls
- Tests run in two modes: native Linux (direct) and under Shadow (intercepted)
- `running_in_shadow()` helper detects the execution environment
- Tests compare both libc wrapper calls AND direct `syscall()` invocations
- CMake `add_linux_tests()` and `add_shadow_tests()` macros register tests for both environments

**Key insight:** Testing both the libc wrapper path AND the raw `syscall()` path. This maps directly to LinBox's LD_PRELOAD + seccomp dual interception.

### gVisor

**Approach:** GoogleTest (C++) with custom syscall matchers.

- Tests in `test/syscalls/linux/` — one file per syscall category
- Custom matchers: `SyscallSucceeds()`, `SyscallSucceedsWithValue()`, `SyscallFailsWithErrno(EINVAL)`
- Tests run in multiple modes: native, ptrace platform, systrap platform, KVM platform
- RAII classes for test resource management
- Same test binary validates behavior across all platforms

**Key insight:** Custom assertion helpers for syscall return values + errno. The `SyscallSucceedsWithValue()` / `SyscallFailsWithErrno()` pattern is excellent and should be adopted.

### proxychains-ng

- `tests/` directory exists but limited information available
- Makefile-driven testing

## 3. Test Harness Pattern for LD_PRELOAD

The fundamental challenge: the test program must be loaded WITH `LD_PRELOAD=liblinbox.so`. This means the test binary cannot be the same process that decides whether the test passed.

### Pattern A: Two-Binary (recommended for integration tests)

```
test-time-preload.c    →  compiled to test-time-preload (the subject)
                           Calls clock_gettime(), checks returned value
                           Returns 0 on success, non-zero on failure

CMakeLists.txt:
  add_executable(test-time-preload test-time-preload.c)
  add_test(NAME time-preload COMMAND test-time-preload)
  set_tests_properties(time-preload PROPERTIES
    ENVIRONMENT "LD_PRELOAD=$<TARGET_FILE:linbox>;LINBOX_SHM=/tmp/test-shm"
  )
```

The test binary is a standalone program. CMake's `set_tests_properties(ENVIRONMENT)` injects `LD_PRELOAD`. CTest runs it and checks the exit code.

### Pattern B: Wrapper Script (for complex scenarios)

```bash
#!/bin/bash
# test-time-wrapper.sh
export LD_PRELOAD="$1"
export LINBOX_SHM="$2"

# Start controller in background
$3 --shm "$2" --time "2025-01-01T00:00:00Z" &
CTRL_PID=$!
sleep 0.1

# Run test subject
./test-time-preload
RESULT=$?

kill $CTRL_PID
exit $RESULT
```

Used when the test needs a controller process running simultaneously.

### Pattern C: In-Process Unit Tests (for non-LD_PRELOAD code)

```c
// sbp.test.c — tests SBP protocol parsing, no LD_PRELOAD needed
#include <criterion/criterion.h>
#include "sbp.h"

Test(sbp, parse_hello_message) {
    uint8_t buf[] = {0x01, 0x00, 0x0C, ...};
    sbp_msg_t msg;
    int rc = sbp_parse(buf, sizeof(buf), &msg);
    cr_assert_eq(rc, 0);
    cr_assert_eq(msg.type, SBP_HELLO);
}
```

These tests compile and run directly — no LD_PRELOAD, no wrapper. Used for pure logic (protocol parsing, shared memory layout, PRNG algorithm, timer queue).

### Recommended Test Topology

```
src/shim/time.c          →  src/shim/time.test.c        (unit, Criterion, no LD_PRELOAD)
src/shim/time.c          →  src/shim/time.preload.c     (integration, standalone binary, WITH LD_PRELOAD)
src/common/sbp.c         →  src/common/sbp.test.c       (unit, Criterion, no LD_PRELOAD)
src/common/shm.c         →  src/common/shm.test.c       (unit, Criterion, no LD_PRELOAD)
src/controller/main.c    →  src/controller/server.test.c (unit, Criterion, no LD_PRELOAD)
tests/e2e/pg-time.sh     →  (end-to-end, shell, Docker)
```

File naming:
- `*.test.c` — unit tests (Criterion, run directly)
- `*.preload.c` — LD_PRELOAD integration tests (standalone binary, run via CTest with ENVIRONMENT)
- `tests/e2e/*.sh` — end-to-end tests (Docker, real services)

## 4. Testing the Unix Socket Server (Controller)

### Unit Testing the Protocol Layer

The SBP (Sim-Box Protocol) message parsing and serialization can be tested purely in-process:

```c
// src/common/sbp.test.c
#include <criterion/criterion.h>
#include "sbp.h"

Test(sbp, serialize_deserialize_roundtrip) {
    sbp_msg_t original = {
        .type = SBP_SET_TIME,
        .payload.set_time = { .tv_sec = 1735689600, .tv_nsec = 0 }
    };

    uint8_t buf[SBP_MAX_MSG_SIZE];
    int len = sbp_serialize(&original, buf, sizeof(buf));
    cr_assert_gt(len, 0);

    sbp_msg_t decoded;
    int rc = sbp_parse(buf, len, &decoded);
    cr_assert_eq(rc, 0);
    cr_assert_eq(decoded.type, SBP_SET_TIME);
    cr_assert_eq(decoded.payload.set_time.tv_sec, 1735689600);
}
```

### Integration Testing the Socket Server

Use a real unix socket in a temporary directory:

```c
// src/controller/server.test.c
#include <criterion/criterion.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <pthread.h>

static char sock_path[128];
static pthread_t server_thread;

void setup(void) {
    snprintf(sock_path, sizeof(sock_path), "/tmp/linbox-test-%d.sock", getpid());
    // Start server in a thread (not a process — same address space)
    pthread_create(&server_thread, NULL, controller_serve, sock_path);
    usleep(10000); // 10ms for server to bind
}

void teardown(void) {
    // Send shutdown command
    controller_shutdown(sock_path);
    pthread_join(server_thread, NULL);
    unlink(sock_path);
}

Test(controller, hello_handshake, .init = setup, .fini = teardown) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);
    cr_assert_eq(connect(fd, (struct sockaddr*)&addr, sizeof(addr)), 0);

    // Send HELLO
    sbp_msg_t hello = { .type = SBP_HELLO };
    uint8_t buf[256];
    int len = sbp_serialize(&hello, buf, sizeof(buf));
    write(fd, buf, len);

    // Read response
    int n = read(fd, buf, sizeof(buf));
    cr_assert_gt(n, 0);

    sbp_msg_t response;
    cr_assert_eq(sbp_parse(buf, n, &response), 0);
    cr_assert_eq(response.type, SBP_HELLO_ACK);

    close(fd);
}
```

**Key pattern:** Run the server in a thread (not a subprocess) so the test can share address space for assertions. Use PID-based socket paths to avoid collisions in parallel test runs.

## 5. Testing Shared Memory Communication

### Unit Testing SHM Layout

```c
// src/common/shm.test.c
#include <criterion/criterion.h>
#include <sys/mman.h>
#include <fcntl.h>
#include "shm-layout.h"
#include "shm.h"

Test(shm, create_and_attach) {
    char path[128];
    snprintf(path, sizeof(path), "/tmp/linbox-shm-test-%d", getpid());

    // Controller side: create
    linbox_shm_t *writer = linbox_shm_create(path);
    cr_assert_not_null(writer);

    // Shim side: attach
    linbox_shm_t *reader = linbox_shm_attach(path);
    cr_assert_not_null(reader);

    // Write from controller, read from shim
    writer->time.tv_sec = 1735689600;
    writer->time.tv_nsec = 123456789;
    __atomic_thread_fence(__ATOMIC_RELEASE);

    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    cr_assert_eq(reader->time.tv_sec, 1735689600);
    cr_assert_eq(reader->time.tv_nsec, 123456789);

    linbox_shm_detach(reader);
    linbox_shm_destroy(writer, path);
}

Test(shm, layout_size_matches) {
    // Verify the SHM layout fits in the expected page size
    cr_assert_leq(sizeof(linbox_shm_t), 4096,
        "SHM layout exceeds single page: %zu bytes", sizeof(linbox_shm_t));
}

Test(shm, atomic_time_read) {
    char path[128];
    snprintf(path, sizeof(path), "/tmp/linbox-shm-test-atomic-%d", getpid());
    linbox_shm_t *shm = linbox_shm_create(path);
    cr_assert_not_null(shm);

    // Simulate controller writing time
    shm->time.tv_sec = 1000;
    shm->time.tv_nsec = 500000000;

    // Simulate shim reading time (same process, but validates the accessor)
    struct timespec ts;
    linbox_shm_read_time(shm, &ts);
    cr_assert_eq(ts.tv_sec, 1000);
    cr_assert_eq(ts.tv_nsec, 500000000);

    linbox_shm_destroy(shm, path);
}
```

### Cross-Process SHM Test (Integration)

```c
// src/common/shm.preload.c — standalone binary for cross-process SHM test
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include "shm-layout.h"
#include "shm.h"

int main(void) {
    const char *path = "/tmp/linbox-shm-xproc-test";
    linbox_shm_t *shm = linbox_shm_create(path);
    if (!shm) return 1;

    shm->time.tv_sec = 2000;
    shm->time.tv_nsec = 0;

    pid_t child = fork();
    if (child == 0) {
        // Child: attach and verify
        linbox_shm_t *child_shm = linbox_shm_attach(path);
        if (!child_shm) _exit(1);
        if (child_shm->time.tv_sec != 2000) _exit(2);

        // Wait for parent to update
        usleep(50000);
        if (child_shm->time.tv_sec != 3000) _exit(3);

        linbox_shm_detach(child_shm);
        _exit(0);
    }

    // Parent: update time after child attaches
    usleep(25000);
    shm->time.tv_sec = 3000;

    int status;
    waitpid(child, &status, 0);
    linbox_shm_destroy(shm, path);

    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}
```

## 6. CMake Integration

### Top-Level CMakeLists.txt Pattern

```cmake
cmake_minimum_required(VERSION 3.20)
project(linbox C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# --- Dependencies ---
include(FetchContent)

option(BUILD_TESTING "Build tests" ON)

if(BUILD_TESTING)
    enable_testing()

    FetchContent_Declare(
        criterion
        GIT_REPOSITORY https://github.com/Snaipe/Criterion.git
        GIT_TAG v2.4.3
    )
    FetchContent_MakeAvailable(criterion)
endif()

# --- Library targets ---
add_library(linbox SHARED
    src/shim/linbox.c
    src/shim/time.c
    src/shim/random.c
    # ...
)
target_link_libraries(linbox PRIVATE dl pthread)

add_executable(linbox-controller
    src/controller/main.c
    src/controller/time-manager.c
    # ...
)

# --- Shared code (static lib for both shim and controller) ---
add_library(linbox-common STATIC
    src/common/sbp.c
    src/common/shm.c
)

# --- Tests ---
if(BUILD_TESTING)
    # Helper function for unit tests (Criterion-based, no LD_PRELOAD)
    function(linbox_add_unit_test NAME SOURCE)
        add_executable(${NAME} ${SOURCE})
        target_link_libraries(${NAME} PRIVATE criterion linbox-common)
        add_test(NAME ${NAME} COMMAND ${NAME})
    endfunction()

    # Helper function for preload integration tests
    function(linbox_add_preload_test NAME SOURCE)
        add_executable(${NAME} ${SOURCE})
        target_link_libraries(${NAME} PRIVATE linbox-common)
        add_test(NAME ${NAME} COMMAND ${NAME})
        set_tests_properties(${NAME} PROPERTIES
            ENVIRONMENT "LD_PRELOAD=$<TARGET_FILE:linbox>"
        )
    endfunction()

    # Unit tests
    linbox_add_unit_test(test-sbp         src/common/sbp.test.c)
    linbox_add_unit_test(test-shm         src/common/shm.test.c)
    linbox_add_unit_test(test-shm-layout  src/common/shm-layout.test.c)
    linbox_add_unit_test(test-time        src/shim/time.test.c)
    linbox_add_unit_test(test-prng        src/shim/prng.test.c)
    linbox_add_unit_test(test-controller  src/controller/server.test.c)

    # LD_PRELOAD integration tests
    linbox_add_preload_test(test-time-preload     src/shim/time.preload.c)
    linbox_add_preload_test(test-random-preload   src/shim/random.preload.c)
    linbox_add_preload_test(test-seccomp-preload  src/shim/seccomp.preload.c)
    linbox_add_preload_test(test-shm-xproc        src/common/shm.preload.c)
endif()
```

### Running Tests

```bash
mkdir build && cd build
cmake .. -DBUILD_TESTING=ON
make
ctest --output-on-failure        # run all tests
ctest -R preload                 # run only preload tests
ctest -R sbp                     # run only SBP tests
ctest --output-junit results.xml # JUnit XML for CI
```

## 7. LD_PRELOAD + seccomp Test Examples

### Testing clock_gettime Interception via LD_PRELOAD

```c
// src/shim/time.preload.c
// Standalone binary — run with LD_PRELOAD=liblinbox.so
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

int main(void) {
    struct timespec ts;
    int failures = 0;

    // Test 1: clock_gettime(CLOCK_REALTIME) should return virtual time
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        fprintf(stderr, "FAIL: clock_gettime returned error\n");
        failures++;
    }

    // The controller should have set time to 2025-01-01 00:00:00 UTC
    // (1735689600 seconds since epoch)
    const time_t expected = 1735689600;
    if (ts.tv_sec != expected) {
        fprintf(stderr, "FAIL: expected tv_sec=%ld, got %ld\n",
                (long)expected, (long)ts.tv_sec);
        failures++;
    }

    // Test 2: gettimeofday should agree
    struct timeval tv;
    gettimeofday(&tv, NULL);
    if (tv.tv_sec != expected) {
        fprintf(stderr, "FAIL: gettimeofday expected %ld, got %ld\n",
                (long)expected, (long)tv.tv_sec);
        failures++;
    }

    // Test 3: time() should agree
    time_t t = time(NULL);
    if (t != expected) {
        fprintf(stderr, "FAIL: time() expected %ld, got %ld\n",
                (long)expected, (long)t);
        failures++;
    }

    if (failures == 0) {
        printf("PASS: all time interception tests passed\n");
    }
    return failures;
}
```

### Testing seccomp SIGSYS Handler

```c
// src/shim/seccomp.preload.c
// Standalone binary — run with LD_PRELOAD=liblinbox.so
// Tests that direct syscalls are caught by seccomp-bpf and handled by SIGSYS
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>

int main(void) {
    int failures = 0;

    // Test 1: Direct syscall should be caught and return virtual time
    struct timespec ts;
    long ret = syscall(SYS_clock_gettime, CLOCK_REALTIME, &ts);
    if (ret != 0) {
        fprintf(stderr, "FAIL: direct syscall returned %ld\n", ret);
        failures++;
    }

    const time_t expected = 1735689600;
    if (ts.tv_sec != expected) {
        fprintf(stderr, "FAIL: direct syscall tv_sec=%ld, expected %ld\n",
                (long)ts.tv_sec, (long)expected);
        failures++;
    }

    // Test 2: getrandom via direct syscall
    unsigned char buf[16];
    ret = syscall(SYS_getrandom, buf, sizeof(buf), 0);
    if (ret != sizeof(buf)) {
        fprintf(stderr, "FAIL: direct getrandom returned %ld\n", ret);
        failures++;
    }

    // Verify determinism: call again with same seed should give same result
    // (requires controller to reset seed between calls, or check a known value)

    if (failures == 0) {
        printf("PASS: all seccomp interception tests passed\n");
    }
    return failures;
}
```

### Unit Testing the SIGSYS Handler Logic (without seccomp)

```c
// src/shim/sigsys.test.c
// Criterion unit test — tests the dispatch logic, not the signal delivery
#include <criterion/criterion.h>
#include <signal.h>
#include <sys/syscall.h>
#include "sigsys.h"

Test(sigsys, dispatch_clock_gettime) {
    // Construct a fake siginfo_t as if seccomp raised SIGSYS
    siginfo_t si = {
        .si_signo = SIGSYS,
        .si_code = SYS_SECCOMP,
        .si_syscall = SYS_clock_gettime,
    };

    // The handler should recognize this syscall and route it
    int handled = sigsys_can_handle(si.si_syscall);
    cr_assert(handled, "SIGSYS handler should recognize SYS_clock_gettime");
}

Test(sigsys, unknown_syscall_passthrough) {
    // Syscalls we don't intercept should pass through
    int handled = sigsys_can_handle(SYS_write);
    cr_assert_not(handled, "SYS_write should not be intercepted");
}
```

## 8. CI Configuration

### GitHub Actions Example

```yaml
name: test
on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v4

      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y cmake build-essential

      - name: Build
        run: |
          cmake -B build -DBUILD_TESTING=ON
          cmake --build build -j$(nproc)

      - name: Unit tests
        run: cd build && ctest -R "^test-" --output-on-failure --output-junit unit-results.xml

      - name: LD_PRELOAD integration tests
        run: cd build && ctest -R "preload" --output-on-failure --output-junit preload-results.xml

      - name: Upload test results
        if: always()
        uses: actions/upload-artifact@v4
        with:
          name: test-results
          path: build/*-results.xml
```

Headless CI works because:
- Criterion tests are self-contained binaries (no display needed)
- LD_PRELOAD tests are self-contained (CTest sets environment)
- seccomp tests need `CAP_SYS_ADMIN` or `--security-opt seccomp=unconfined` — in CI, run in a Docker container or use `sudo`

## 9. Summary of Recommendations

### Framework Choice

| Test type | Framework | File pattern | LD_PRELOAD? |
|-----------|-----------|-------------|-------------|
| Unit (logic) | Criterion | `*.test.c` | No |
| Unit (mocked) | CMocka | `*.test.c` | No |
| LD_PRELOAD integration | Plain C + CTest | `*.preload.c` | Yes |
| Controller integration | Criterion | `*.test.c` | No |
| End-to-end | Shell scripts | `tests/e2e/*.sh` | Via Docker |

### File Organization

```
src/
  shim/
    time.c
    time.test.c           ← unit test (Criterion): test shim_time_read() logic
    time.preload.c         ← integration test (standalone): verify interception
    random.c
    random.test.c
    random.preload.c
    seccomp.c
    seccomp.preload.c      ← test seccomp SIGSYS handler end-to-end
    sigsys.c
    sigsys.test.c          ← unit test dispatch logic
    prng.c
    prng.test.c            ← unit test ChaCha20 output
  common/
    sbp.c
    sbp.test.c             ← test serialize/deserialize roundtrip
    shm.c
    shm.test.c             ← test SHM create/attach/read/write
    shm.preload.c          ← cross-process SHM test
  controller/
    main.c
    server.test.c          ← test socket server with real unix socket
    time-manager.c
    time-manager.test.c    ← test time calculation logic
tests/
  e2e/
    pg-time.sh             ← PostgreSQL + LinBox: SELECT now()
    pg-random.sh           ← deterministic uuid_generate_v4()
```

### Key Design Decisions

1. **Criterion for unit tests** — automatic discovery, fork isolation, signal testing, clean syntax
2. **Standalone binaries for LD_PRELOAD tests** — the test subject must be loaded with the shim; it cannot be the test runner itself
3. **CTest ENVIRONMENT for LD_PRELOAD injection** — CMake-native, no wrapper scripts needed for simple cases
4. **Co-located tests** — `time.c` and `time.test.c` in the same directory per AGENTS.md
5. **Three test layers** — unit (fast, in-process), integration (LD_PRELOAD, fork), e2e (Docker, real services)
6. **PID-based temp paths** — for socket and SHM paths in tests, avoiding collisions in parallel runs

### What NOT to Do

- Do not try to load the test framework itself with LD_PRELOAD — the Criterion runner is a separate binary
- Do not use `dlopen()` to load liblinbox.so in tests — use the real LD_PRELOAD mechanism to test what users actually experience
- Do not put seccomp filter installation in unit tests — it affects the entire process; use `.preload.c` integration tests for that
- Do not use Google Test just because it is popular — the C++ requirement adds unnecessary friction for a C project

---

[← Back](README.md)
