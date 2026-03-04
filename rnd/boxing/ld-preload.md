# LD_PRELOAD: Syscall/Libc Interception for Deterministic Simulation

Deep technical research on the LD_PRELOAD mechanism for intercepting libc functions in Linux containers, with focus on building a deterministic simulation sandbox.

## Table of Contents

- [How It Works Technically](#how-it-works-technically)
- [Interceptable Libc Functions by Category](#interceptable-libc-functions-by-category)
- [Code Patterns](#code-patterns)
- [Known Limitations and Bypasses](#known-limitations-and-bypasses)
- [Performance Overhead](#performance-overhead)
- [Existing Mature LD_PRELOAD Projects](#existing-mature-ld_preload-projects)
- [Comparison with Alternatives](#comparison-with-alternatives)
- [Implications for LinBox](#implications-for-linbox)
- [Sources](#sources)
- [.so ↔ Sim Transport: IPC Options](#so--sim-transport-ipc-options)
- [Binary Protocol: Wire Format](#binary-protocol-wire-format)
- [Optimization: Policy-Based Caching](#optimization-policy-based-caching)
- [LinBox Architecture Summary](#linbox-architecture-summary)
- [Transport Sources](#transport-sources)

---

## How It Works Technically

### 1. Dynamic Linker Resolution

When a dynamically linked program starts, the kernel loads the ELF binary and transfers control to the dynamic linker (`ld-linux-x86-64.so.2`, also known as `ld.so`). The dynamic linker is responsible for:

1. Parsing the ELF headers of the executable
2. Loading all required shared libraries (from `DT_NEEDED` entries)
3. Resolving symbol references between the executable and its libraries
4. Setting up the PLT (Procedure Linkage Table) and GOT (Global Offset Table)

The symbol resolution follows a strict **search order**:

```
1. LD_PRELOAD libraries (loaded first, before everything else)
2. The executable itself
3. DT_NEEDED libraries in dependency order
4. Default system libraries (libc, libpthread, etc.)
```

When `LD_PRELOAD=/path/to/hook.so` is set, the dynamic linker loads `hook.so` **before any other library**, including libc. If `hook.so` defines a symbol with the same name as a libc function (e.g., `clock_gettime`), the linker binds all external references to that name to the preloaded version.

### 2. PLT/GOT Mechanism

External function calls in ELF binaries go through a two-stage indirection:

**PLT (Procedure Linkage Table):** A read-only table of stub entries in the `.plt` section. Each stub performs an indirect jump through a corresponding GOT entry.

**GOT (Global Offset Table):** A writable table in the `.got.plt` section. Initially, GOT entries point back to PLT resolver stubs. After the first call (lazy binding) or at load time (eager binding with `LD_BIND_NOW`), the dynamic linker patches the GOT entry with the resolved function address.

```
Program calls clock_gettime():
  → JMP to PLT stub for clock_gettime
    → JMP through GOT[clock_gettime]
      → (first call) dynamic linker resolves symbol
        → GOT patched to point to LD_PRELOAD version
      → (subsequent calls) direct jump to LD_PRELOAD version
```

With `LD_PRELOAD`, the dynamic linker resolves the GOT entry to the **preloaded library's implementation** instead of libc's, because it appears first in the search order.

### 3. Symbol Interposition

Symbol interposition is the mechanism by which a symbol defined in a preloaded library "shadows" the same symbol in a later library. The key rules:

- **Only globally visible symbols** can be interposed (symbols with `STV_DEFAULT` visibility)
- **Hidden symbols** (`STV_HIDDEN`, `__attribute__((visibility("hidden")))`) cannot be interposed
- **Internal glibc aliases** (like `__GI_open`, `__libc_open`) bypass interposition entirely
- Interposition happens at the **PLT level** — only calls that go through the PLT are affected

### 4. dlsym(RTLD_NEXT, ...) Mechanism

The interposing library typically needs to call the **original** function after doing its work. This is achieved through `dlsym()` with the special handle `RTLD_NEXT`:

```c
#define _GNU_SOURCE
#include <dlfcn.h>

// RTLD_NEXT means: "find the next occurrence of this symbol
// in the library search order, starting AFTER the current library"

typedef int (*real_clock_gettime_t)(clockid_t, struct timespec *);

static real_clock_gettime_t real_clock_gettime = NULL;

int clock_gettime(clockid_t clk_id, struct timespec *tp) {
    if (!real_clock_gettime) {
        real_clock_gettime = (real_clock_gettime_t)dlsym(RTLD_NEXT, "clock_gettime");
    }

    // Call the real function
    int ret = real_clock_gettime(clk_id, tp);

    // Modify the result (e.g., apply time offset)
    if (ret == 0) {
        tp->tv_sec += time_offset;
    }

    return ret;
}
```

**How `dlsym` determines "me" (the caller):** `dlsym` examines the return address on the stack to identify which ELF object the caller belongs to. It then searches for the symbol starting from the **next** library in the load order after that object. This is why `RTLD_NEXT` does not need an explicit "self" parameter.

**Lazy resolution pattern (preferred over constructor):** Resolving the real function pointer on first use (as shown above) is more robust than using `__attribute__((constructor))`. Constructor-based resolution fails when other constructors (e.g., from libselinux in ssh) call the hooked function before the hook's constructor runs.

### 5. Constructor and Destructor Functions

```c
__attribute__((constructor))
static void init(void) {
    // Runs before main() — set up state, open log files
    // WARNING: Do NOT resolve function pointers here
    // Other libraries' constructors may call hooked functions
    // before this constructor runs
}

__attribute__((destructor))
static void fini(void) {
    // Runs after main() returns — flush logs, clean up
}
```

---

## Interceptable Libc Functions by Category

### Time Functions

| Function | Header | Notes |
|---|---|---|
| `time()` | `<time.h>` | Returns seconds since epoch. **vDSO-accelerated** on x86-64 |
| `gettimeofday()` | `<sys/time.h>` | Microsecond resolution. **vDSO-accelerated** on x86-64 |
| `clock_gettime()` | `<time.h>` | Nanosecond resolution, multiple clock IDs. **vDSO-accelerated** for `CLOCK_REALTIME`, `CLOCK_MONOTONIC`, `CLOCK_REALTIME_COARSE`, `CLOCK_MONOTONIC_COARSE` |
| `clock_getres()` | `<time.h>` | Returns clock resolution |
| `clock_nanosleep()` | `<time.h>` | Sleep with specific clock |
| `nanosleep()` | `<time.h>` | High-resolution sleep |
| `usleep()` | `<unistd.h>` | Microsecond sleep (POSIX.1-2001, obsolete) |
| `sleep()` | `<unistd.h>` | Second-resolution sleep |
| `alarm()` | `<unistd.h>` | Schedule SIGALRM delivery |
| `timer_create()` | `<time.h>` | Create a POSIX timer |
| `timer_settime()` | `<time.h>` | Arm/disarm a POSIX timer |
| `timer_gettime()` | `<time.h>` | Get timer remaining time |
| `timer_delete()` | `<time.h>` | Delete a POSIX timer |
| `timerfd_create()` | `<sys/timerfd.h>` | Create timer that notifies via file descriptor |
| `timerfd_settime()` | `<sys/timerfd.h>` | Arm timerfd timer |
| `timerfd_gettime()` | `<sys/timerfd.h>` | Get timerfd remaining time |
| `clock()` | `<time.h>` | CPU time used by process |
| `times()` | `<sys/times.h>` | Process times |
| `timespec_get()` | `<time.h>` | C11 time function |
| `ftime()` | `<sys/timeb.h>` | Deprecated, millisecond time |

**Critical for deterministic simulation:** All clock IDs must be handled:
- `CLOCK_REALTIME` — wall-clock time, settable
- `CLOCK_MONOTONIC` — monotonically increasing, not settable
- `CLOCK_MONOTONIC_RAW` — hardware-based, no NTP adjustment
- `CLOCK_MONOTONIC_COARSE` — fast but lower resolution
- `CLOCK_REALTIME_COARSE` — fast but lower resolution
- `CLOCK_BOOTTIME` — includes time suspended
- `CLOCK_PROCESS_CPUTIME_ID` — per-process CPU time
- `CLOCK_THREAD_CPUTIME_ID` — per-thread CPU time

### Network Functions

| Function | Header | Notes |
|---|---|---|
| `socket()` | `<sys/socket.h>` | Create endpoint |
| `connect()` | `<sys/socket.h>` | Initiate connection |
| `accept()` | `<sys/socket.h>` | Accept connection |
| `accept4()` | `<sys/socket.h>` | Accept with flags (Linux-specific) |
| `bind()` | `<sys/socket.h>` | Bind address to socket |
| `listen()` | `<sys/socket.h>` | Mark socket as passive |
| `send()` | `<sys/socket.h>` | Send data |
| `recv()` | `<sys/socket.h>` | Receive data |
| `sendto()` | `<sys/socket.h>` | Send to specific address |
| `recvfrom()` | `<sys/socket.h>` | Receive with sender address |
| `sendmsg()` | `<sys/socket.h>` | Send with message structure |
| `recvmsg()` | `<sys/socket.h>` | Receive with message structure |
| `sendmmsg()` | `<sys/socket.h>` | Send multiple messages |
| `recvmmsg()` | `<sys/socket.h>` | Receive multiple messages |
| `shutdown()` | `<sys/socket.h>` | Shutdown part of connection |
| `setsockopt()` | `<sys/socket.h>` | Set socket options |
| `getsockopt()` | `<sys/socket.h>` | Get socket options |
| `getsockname()` | `<sys/socket.h>` | Get local address |
| `getpeername()` | `<sys/socket.h>` | Get remote address |
| `socketpair()` | `<sys/socket.h>` | Create connected socket pair |
| `poll()` | `<poll.h>` | Wait for events on file descriptors |
| `ppoll()` | `<poll.h>` | poll with signal mask |
| `select()` | `<sys/select.h>` | Synchronous I/O multiplexing |
| `pselect()` | `<sys/select.h>` | select with signal mask |
| `epoll_create()` | `<sys/epoll.h>` | Create epoll instance |
| `epoll_create1()` | `<sys/epoll.h>` | epoll_create with flags |
| `epoll_ctl()` | `<sys/epoll.h>` | Control epoll instance |
| `epoll_wait()` | `<sys/epoll.h>` | Wait for epoll events |
| `epoll_pwait()` | `<sys/epoll.h>` | epoll_wait with signal mask |
| `epoll_pwait2()` | `<sys/epoll.h>` | epoll_wait with timespec |
| `getaddrinfo()` | `<netdb.h>` | DNS resolution |
| `gethostbyname()` | `<netdb.h>` | Legacy DNS resolution |
| `gethostbyname2()` | `<netdb.h>` | DNS with address family |
| `gethostbyaddr()` | `<netdb.h>` | Reverse DNS |
| `getifaddrs()` | `<ifaddrs.h>` | Get interface addresses |
| `if_nametoindex()` | `<net/if.h>` | Interface name to index |

### Filesystem Functions

| Function | Header | Notes |
|---|---|---|
| `open()` | `<fcntl.h>` | Open file. **Note:** glibc may internally use `openat()` |
| `openat()` | `<fcntl.h>` | Open relative to directory fd |
| `openat2()` | (Linux 5.6+) | Extended openat with resolve flags |
| `creat()` | `<fcntl.h>` | Create file (equivalent to open with O_CREAT\|O_WRONLY\|O_TRUNC) |
| `close()` | `<unistd.h>` | Close file descriptor |
| `read()` | `<unistd.h>` | Read from fd |
| `write()` | `<unistd.h>` | Write to fd |
| `pread()` | `<unistd.h>` | Read at offset |
| `pwrite()` | `<unistd.h>` | Write at offset |
| `readv()` | `<sys/uio.h>` | Scatter read |
| `writev()` | `<sys/uio.h>` | Gather write |
| `preadv()` | `<sys/uio.h>` | Scatter read at offset |
| `pwritev()` | `<sys/uio.h>` | Gather write at offset |
| `lseek()` | `<unistd.h>` | Reposition file offset |
| `stat()` | `<sys/stat.h>` | File status. **glibc may redirect to `fstatat()`** |
| `fstat()` | `<sys/stat.h>` | Status of open fd |
| `lstat()` | `<sys/stat.h>` | Status of symlink |
| `fstatat()` | `<sys/stat.h>` | Status relative to directory fd |
| `statx()` | `<sys/stat.h>` | Extended file status (Linux 4.11+) |
| `access()` | `<unistd.h>` | Check file permissions |
| `faccessat()` | `<unistd.h>` | access relative to directory fd |
| `mkdir()` | `<sys/stat.h>` | Create directory |
| `mkdirat()` | `<sys/stat.h>` | mkdir relative to directory fd |
| `rmdir()` | `<unistd.h>` | Remove directory |
| `unlink()` | `<unistd.h>` | Remove file |
| `unlinkat()` | `<unistd.h>` | unlink relative to directory fd |
| `rename()` | `<stdio.h>` | Rename file |
| `renameat()` | `<stdio.h>` | rename relative to directory fd |
| `renameat2()` | (Linux 3.15+) | rename with flags |
| `link()` | `<unistd.h>` | Create hard link |
| `linkat()` | `<unistd.h>` | link relative to directory fd |
| `symlink()` | `<unistd.h>` | Create symbolic link |
| `symlinkat()` | `<unistd.h>` | symlink relative to directory fd |
| `readlink()` | `<unistd.h>` | Read symbolic link |
| `readlinkat()` | `<unistd.h>` | readlink relative to directory fd |
| `opendir()` | `<dirent.h>` | Open directory stream |
| `readdir()` | `<dirent.h>` | Read directory entry |
| `closedir()` | `<dirent.h>` | Close directory stream |
| `scandir()` | `<dirent.h>` | Scan directory |
| `truncate()` | `<unistd.h>` | Truncate file to length |
| `ftruncate()` | `<unistd.h>` | Truncate open fd |
| `chmod()` | `<sys/stat.h>` | Change file mode |
| `fchmod()` | `<sys/stat.h>` | Change mode of open fd |
| `fchmodat()` | `<sys/stat.h>` | chmod relative to directory fd |
| `chown()` | `<unistd.h>` | Change file owner |
| `fchown()` | `<unistd.h>` | Change owner of open fd |
| `lchown()` | `<unistd.h>` | Change owner of symlink |
| `fchownat()` | `<unistd.h>` | chown relative to directory fd |
| `dup()` | `<unistd.h>` | Duplicate fd |
| `dup2()` | `<unistd.h>` | Duplicate fd to specific number |
| `dup3()` | `<unistd.h>` | dup2 with flags |
| `fcntl()` | `<fcntl.h>` | File control operations |
| `ioctl()` | `<sys/ioctl.h>` | Device control |
| `fsync()` | `<unistd.h>` | Synchronize file to disk |
| `fdatasync()` | `<unistd.h>` | Synchronize file data |
| `sync()` | `<unistd.h>` | Synchronize all filesystems |
| `syncfs()` | `<unistd.h>` | Synchronize filesystem containing fd |
| `fopen()` | `<stdio.h>` | Open stream (calls `open`/`openat` internally) |
| `fclose()` | `<stdio.h>` | Close stream |
| `fread()` | `<stdio.h>` | Read from stream |
| `fwrite()` | `<stdio.h>` | Write to stream |
| `fprintf()` | `<stdio.h>` | Formatted output to stream |
| `fgets()` | `<stdio.h>` | Read line from stream |
| `fputs()` | `<stdio.h>` | Write string to stream |

**Important:** `fopen` -> `open` and `fwrite` -> `write` calls within glibc use **internal aliases** (`__GI_open`, `__libc_write`) that bypass PLT. Intercepting `write()` will NOT catch writes made by glibc's `fwrite()` internally.

### Process Functions

| Function | Header | Notes |
|---|---|---|
| `fork()` | `<unistd.h>` | Create child process |
| `vfork()` | `<unistd.h>` | Create child (shares parent memory) |
| `clone()` | `<sched.h>` | Create child with fine-grained control (Linux-specific) |
| `clone3()` | (Linux 5.3+) | Extended clone |
| `execve()` | `<unistd.h>` | Execute program |
| `execvp()` | `<unistd.h>` | Execute with PATH search |
| `execvpe()` | `<unistd.h>` | Execute with PATH and environment |
| `execl()`, `execle()`, `execlp()` | `<unistd.h>` | exec family variants |
| `posix_spawn()` | `<spawn.h>` | Spawn process (combines fork+exec) |
| `posix_spawnp()` | `<spawn.h>` | posix_spawn with PATH search |
| `wait()` | `<sys/wait.h>` | Wait for child process |
| `waitpid()` | `<sys/wait.h>` | Wait for specific child |
| `wait4()` | `<sys/wait.h>` | Wait with resource usage |
| `waitid()` | `<sys/wait.h>` | Wait with extended options |
| `getpid()` | `<unistd.h>` | Get process ID |
| `getppid()` | `<unistd.h>` | Get parent process ID |
| `getuid()` | `<unistd.h>` | Get user ID |
| `geteuid()` | `<unistd.h>` | Get effective user ID |
| `getgid()` | `<unistd.h>` | Get group ID |
| `getegid()` | `<unistd.h>` | Get effective group ID |
| `setuid()` | `<unistd.h>` | Set user ID |
| `setgid()` | `<unistd.h>` | Set group ID |
| `setsid()` | `<unistd.h>` | Create new session |
| `getpgid()` | `<unistd.h>` | Get process group ID |
| `setpgid()` | `<unistd.h>` | Set process group ID |
| `exit()` | `<stdlib.h>` | Terminate process |
| `_exit()` | `<unistd.h>` | Terminate immediately |
| `atexit()` | `<stdlib.h>` | Register exit handler |

**Important for deterministic simulation:** `fork()` must propagate `LD_PRELOAD` to child processes. The `execve()` family must ensure `LD_PRELOAD` is preserved in the new environment, or the child process will escape the sandbox.

### Random Functions

| Function | Header | Notes |
|---|---|---|
| `getrandom()` | `<sys/random.h>` | Kernel random (Linux 3.17+) |
| `getentropy()` | `<unistd.h>` | Fill buffer with random bytes |
| `rand()` | `<stdlib.h>` | Pseudo-random (not cryptographic) |
| `srand()` | `<stdlib.h>` | Seed PRNG |
| `random()` | `<stdlib.h>` | Better PRNG |
| `srandom()` | `<stdlib.h>` | Seed better PRNG |
| `rand_r()` | `<stdlib.h>` | Thread-safe PRNG |
| `arc4random()` | `<stdlib.h>` | Cryptographic PRNG (glibc 2.36+) |
| `arc4random_buf()` | `<stdlib.h>` | Fill buffer with arc4random |
| `arc4random_uniform()` | `<stdlib.h>` | Uniform random in range |
| `open("/dev/urandom")` | — | Must intercept `open()` and check path |
| `open("/dev/random")` | — | Must intercept `open()` and check path |

**Critical for determinism:** `/dev/urandom` and `/dev/random` reads must be intercepted at the `open()` level by checking the path argument and redirecting to a deterministic PRNG. Also intercept `read()` on fds that were opened to these paths.

### Signal Functions

| Function | Header | Notes |
|---|---|---|
| `signal()` | `<signal.h>` | Simple signal handler (avoid — use sigaction) |
| `sigaction()` | `<signal.h>` | Examine/change signal action |
| `kill()` | `<signal.h>` | Send signal to process |
| `raise()` | `<signal.h>` | Send signal to calling thread |
| `sigprocmask()` | `<signal.h>` | Examine/change blocked signals |
| `pthread_sigmask()` | `<signal.h>` | Thread-specific signal mask |
| `sigsuspend()` | `<signal.h>` | Wait for signal |
| `sigwait()` | `<signal.h>` | Wait for signal synchronously |
| `sigwaitinfo()` | `<signal.h>` | Wait with signal info |
| `sigtimedwait()` | `<signal.h>` | Wait with timeout |
| `signalfd()` | `<sys/signalfd.h>` | File descriptor for signals |
| `sigqueue()` | `<signal.h>` | Queue signal with data |
| `tgkill()` | (Linux-specific) | Send signal to specific thread |
| `tkill()` | (Linux-specific) | Send signal to thread (deprecated) |

### Memory Functions

| Function | Header | Notes |
|---|---|---|
| `mmap()` | `<sys/mman.h>` | Map files/devices into memory |
| `mmap64()` | `<sys/mman.h>` | Large file mmap |
| `munmap()` | `<sys/mman.h>` | Unmap memory |
| `mprotect()` | `<sys/mman.h>` | Set memory protection |
| `madvise()` | `<sys/mman.h>` | Give advice about memory usage |
| `msync()` | `<sys/mman.h>` | Synchronize mapped memory |
| `mlock()` | `<sys/mman.h>` | Lock memory in RAM |
| `munlock()` | `<sys/mman.h>` | Unlock memory |
| `mlockall()` | `<sys/mman.h>` | Lock all memory |
| `mremap()` | `<sys/mman.h>` | Remap memory (Linux-specific) |
| `brk()` | `<unistd.h>` | Change data segment size |
| `sbrk()` | `<unistd.h>` | Increment data segment |
| `malloc()` | `<stdlib.h>` | Allocate memory |
| `calloc()` | `<stdlib.h>` | Allocate zeroed memory |
| `realloc()` | `<stdlib.h>` | Resize allocation |
| `free()` | `<stdlib.h>` | Free allocation |
| `posix_memalign()` | `<stdlib.h>` | Aligned allocation |
| `aligned_alloc()` | `<stdlib.h>` | C11 aligned allocation |
| `memalign()` | `<malloc.h>` | Legacy aligned allocation |
| `valloc()` | `<stdlib.h>` | Page-aligned allocation |

### Threading Functions

| Function | Header | Notes |
|---|---|---|
| `pthread_create()` | `<pthread.h>` | Create thread |
| `pthread_join()` | `<pthread.h>` | Wait for thread termination |
| `pthread_detach()` | `<pthread.h>` | Detach thread |
| `pthread_exit()` | `<pthread.h>` | Terminate calling thread |
| `pthread_mutex_lock()` | `<pthread.h>` | Lock mutex |
| `pthread_mutex_trylock()` | `<pthread.h>` | Try to lock mutex |
| `pthread_mutex_timedlock()` | `<pthread.h>` | Lock with timeout |
| `pthread_mutex_unlock()` | `<pthread.h>` | Unlock mutex |
| `pthread_mutex_init()` | `<pthread.h>` | Initialize mutex |
| `pthread_mutex_destroy()` | `<pthread.h>` | Destroy mutex |
| `pthread_rwlock_rdlock()` | `<pthread.h>` | Read-lock rwlock |
| `pthread_rwlock_wrlock()` | `<pthread.h>` | Write-lock rwlock |
| `pthread_rwlock_unlock()` | `<pthread.h>` | Unlock rwlock |
| `pthread_cond_wait()` | `<pthread.h>` | Wait on condition variable |
| `pthread_cond_timedwait()` | `<pthread.h>` | Wait with timeout |
| `pthread_cond_signal()` | `<pthread.h>` | Signal one waiter |
| `pthread_cond_broadcast()` | `<pthread.h>` | Signal all waiters |
| `sem_wait()` | `<semaphore.h>` | Wait on semaphore |
| `sem_post()` | `<semaphore.h>` | Post semaphore |
| `sem_timedwait()` | `<semaphore.h>` | Wait with timeout |
| `futex()` | `<linux/futex.h>` | Fast userspace mutex (Linux-specific) |

### Miscellaneous Functions Relevant to Simulation

| Function | Header | Notes |
|---|---|---|
| `syscall()` | `<unistd.h>` | Generic syscall wrapper — **must intercept** to prevent direct syscalls bypassing specific wrappers |
| `prctl()` | `<sys/prctl.h>` | Process control |
| `sysinfo()` | `<sys/sysinfo.h>` | System information |
| `uname()` | `<sys/utsname.h>` | System identification |
| `getenv()` | `<stdlib.h>` | Get environment variable |
| `setenv()` | `<stdlib.h>` | Set environment variable |
| `gethostname()` | `<unistd.h>` | Get hostname |
| `getrusage()` | `<sys/resource.h>` | Get resource usage |

---

## Code Patterns

### Minimal Interception Template

```c
// hook.c — compile with: gcc -shared -fPIC -o hook.so hook.c -ldl
#define _GNU_SOURCE
#include <dlfcn.h>
#include <time.h>
#include <stdint.h>

// Lazy-resolved function pointer (thread-safe with volatile)
static int (*real_clock_gettime)(clockid_t, struct timespec *) = NULL;

static void resolve_real(void) {
    if (!real_clock_gettime) {
        real_clock_gettime = dlsym(RTLD_NEXT, "clock_gettime");
        if (!real_clock_gettime) {
            // Fallback: direct syscall
            // This should never happen with dynamic libc
        }
    }
}

int clock_gettime(clockid_t clk_id, struct timespec *tp) {
    resolve_real();

    int ret = real_clock_gettime(clk_id, tp);

    if (ret == 0 && (clk_id == CLOCK_REALTIME || clk_id == CLOCK_REALTIME_COARSE)) {
        // Apply simulation time offset
        int64_t offset = get_sim_time_offset(); // from shared memory or env var
        tp->tv_sec += offset;
    }

    return ret;
}
```

### Thread-Safe Pattern with Bootstrap Guard

```c
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

// Bootstrap buffer for malloc during dlsym resolution
static char bootstrap_buf[65536];
static size_t bootstrap_offset = 0;
static int in_bootstrap = 0;

static void *(*real_malloc)(size_t) = NULL;
static void (*real_free)(void *) = NULL;

void *malloc(size_t size) {
    if (real_malloc) {
        // Normal path: call real malloc, then do our work
        void *ptr = real_malloc(size);
        // ... logging, tracking, etc.
        return ptr;
    }

    // Bootstrap path: dlsym itself calls malloc
    if (in_bootstrap) {
        // Return from static buffer — never freed during bootstrap
        void *ptr = bootstrap_buf + bootstrap_offset;
        bootstrap_offset += (size + 15) & ~15; // align to 16
        return ptr;
    }

    // First call — resolve the real function
    in_bootstrap = 1;
    real_malloc = dlsym(RTLD_NEXT, "malloc");
    real_free = dlsym(RTLD_NEXT, "free");
    in_bootstrap = 0;

    return real_malloc(size);
}

void free(void *ptr) {
    // Ignore frees of bootstrap buffer allocations
    if (ptr >= (void *)bootstrap_buf &&
        ptr < (void *)(bootstrap_buf + sizeof(bootstrap_buf))) {
        return;
    }

    if (real_free) {
        real_free(ptr);
    }
}
```

### Network Interception Pattern (proxychains-style)

```c
#define _GNU_SOURCE
#include <dlfcn.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static int (*real_connect)(int, const struct sockaddr *, socklen_t) = NULL;

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    if (!real_connect)
        real_connect = dlsym(RTLD_NEXT, "connect");

    if (addr->sa_family == AF_INET) {
        struct sockaddr_in *sin = (struct sockaddr_in *)addr;

        // Log the connection attempt
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip));
        int port = ntohs(sin->sin_port);

        // Option 1: Redirect to simulation proxy
        // Option 2: Record for deterministic replay
        // Option 3: Block outgoing connections entirely

        // For simulation: redirect to localhost simulation server
        sin->sin_addr.s_addr = inet_addr("127.0.0.1");
        sin->sin_port = htons(SIM_PROXY_PORT);
    }

    return real_connect(sockfd, addr, addrlen);
}
```

### /dev/urandom Interception for Deterministic Randomness

```c
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

static int (*real_open)(const char *, int, ...) = NULL;
static ssize_t (*real_read)(int, void *, size_t) = NULL;

// Track which fds point to /dev/urandom
#define MAX_FDS 65536
static char fd_is_random[MAX_FDS];

// Deterministic PRNG (xoshiro256**)
static uint64_t prng_state[4] = {1, 2, 3, 4}; // seed from env or config

static uint64_t prng_next(void) {
    uint64_t result = prng_state[1] * 5;
    result = ((result << 7) | (result >> 57)) * 9;
    uint64_t t = prng_state[1] << 17;
    prng_state[2] ^= prng_state[0];
    prng_state[3] ^= prng_state[1];
    prng_state[1] ^= prng_state[2];
    prng_state[0] ^= prng_state[3];
    prng_state[2] ^= t;
    prng_state[3] = (prng_state[3] << 45) | (prng_state[3] >> 19);
    return result;
}

int open(const char *pathname, int flags, ...) {
    if (!real_open)
        real_open = dlsym(RTLD_NEXT, "open");

    // Intercept /dev/urandom and /dev/random
    if (strcmp(pathname, "/dev/urandom") == 0 ||
        strcmp(pathname, "/dev/random") == 0) {
        // Open /dev/null as a placeholder fd
        int fd = real_open("/dev/null", O_RDONLY);
        if (fd >= 0 && fd < MAX_FDS) {
            fd_is_random[fd] = 1;
        }
        return fd;
    }

    // Pass through for all other files
    va_list args;
    va_start(args, flags);
    mode_t mode = va_arg(args, mode_t);
    va_end(args);
    return real_open(pathname, flags, mode);
}

ssize_t read(int fd, void *buf, size_t count) {
    if (!real_read)
        real_read = dlsym(RTLD_NEXT, "read");

    // If this fd was opened as /dev/urandom, fill with deterministic data
    if (fd >= 0 && fd < MAX_FDS && fd_is_random[fd]) {
        uint8_t *out = (uint8_t *)buf;
        size_t remaining = count;
        while (remaining >= 8) {
            uint64_t val = prng_next();
            memcpy(out, &val, 8);
            out += 8;
            remaining -= 8;
        }
        if (remaining > 0) {
            uint64_t val = prng_next();
            memcpy(out, &val, remaining);
        }
        return (ssize_t)count;
    }

    return real_read(fd, buf, count);
}
```

### Compile and Use

```bash
# Compile the interception library
gcc -shared -fPIC -o libsimhook.so hook.c -ldl -pthread

# Run a program under interception
LD_PRELOAD=/path/to/libsimhook.so ./my_program

# Multiple libraries can be preloaded (colon-separated)
LD_PRELOAD=/path/to/libfaketime.so:/path/to/libsimhook.so ./my_program

# In Docker
docker run -e LD_PRELOAD=/hooks/libsimhook.so \
           -v /path/to/libsimhook.so:/hooks/libsimhook.so \
           my_image
```

---

## Known Limitations and Bypasses

### 1. Static Linking — Complete Bypass

**Severity: Critical**

Statically linked binaries include all library code directly in the executable. The dynamic linker (`ld.so`) is not involved, so `LD_PRELOAD` has no effect whatsoever.

```bash
# Statically linked — LD_PRELOAD has no effect
gcc -static -o myprogram myprogram.c
LD_PRELOAD=./hook.so ./myprogram  # hook is completely ignored

# Check if a binary is statically linked
file myprogram
# "statically linked" = LD_PRELOAD won't work
# "dynamically linked" = LD_PRELOAD will work

ldd myprogram
# "not a dynamic executable" = statically linked
```

**Affected software:** Some Alpine Linux images use musl libc with static linking by default. Rust and Go programs are frequently statically linked.

**Mitigation:** Use seccomp-bpf or ptrace for static binaries. In containers, ensure base images use dynamic linking.

### 2. Go Runtime — Raw Syscalls

**Severity: Critical**

The Go runtime makes syscalls directly via assembly instructions (`SYSCALL` on x86-64, `SVC` on ARM64) without going through libc. This is done in `runtime/internal/syscall.Syscall6`.

```go
// Go's runtime/internal/syscall package uses raw assembly:
// TEXT ·Syscall6(SB),NOSPLIT,$0
//   MOVQ    num+0(FP), AX    // syscall number
//   MOVQ    a1+8(FP), DI     // arg 1
//   ...
//   SYSCALL                   // raw syscall instruction
```

This means:
- `time.Now()` in Go uses `clock_gettime` via `vDSO` or raw `SYSCALL`, never via libc
- Network I/O (`net.Dial`, `net.Listen`) uses raw syscalls
- File I/O (`os.Open`, `os.Read`) uses raw syscalls
- `LD_PRELOAD` hooks are completely invisible to Go programs

**Partial exception:** Go programs using CGO may call some functions through libc. If `CGO_ENABLED=1`, functions called via CGO will go through the dynamic linker. However, the Go runtime's own syscalls still bypass libc.

**Mitigation options:**
- Use `ptrace(PTRACE_SYSEMU)` to intercept all syscalls
- Use `seccomp(SECCOMP_RET_TRAP)` or `seccomp(SECCOMP_RET_USER_NOTIF)` for kernel-level interception
- Use Meta's `hermit` or Google's `gVisor` (sentry) which intercept at the syscall level

### 3. vDSO (Virtual Dynamic Shared Object)

**Severity: High**

The kernel maps a small shared library (the vDSO) into every process's address space. This library provides userspace implementations of frequently-called syscalls, avoiding the overhead of kernel mode transitions.

**vDSO-accelerated functions on x86-64:**
- `__vdso_clock_gettime()` (for `CLOCK_REALTIME`, `CLOCK_MONOTONIC`, `CLOCK_REALTIME_COARSE`, `CLOCK_MONOTONIC_COARSE`)
- `__vdso_gettimeofday()`
- `__vdso_time()`
- `__vdso_getcpu()`

**How vDSO interacts with LD_PRELOAD:**

The relationship is nuanced. glibc's `clock_gettime()` wrapper typically calls the vDSO version. When you intercept `clock_gettime()` via `LD_PRELOAD`, you intercept the **glibc wrapper**, which means:

- If the program calls `clock_gettime()` as a normal function call through the PLT, your hook **will** intercept it
- If the program resolves and calls `__vdso_clock_gettime()` directly (rare, but possible), your hook **will not** intercept it
- glibc's internal code that calls `clock_gettime()` using `__GI_clock_gettime` will **not** go through your hook

**libfaketime's approach:** libfaketime intercepts the glibc wrappers (`clock_gettime`, `gettimeofday`, `time`) which works for most programs because they call these functions through the standard library interface. The vDSO is an implementation detail of how glibc fulfills the call — but since libfaketime replaces the glibc wrapper itself, the vDSO is never reached.

**When vDSO is a problem:**
- Programs that use `dlsym()` to find and call `__vdso_clock_gettime` directly
- Programs that parse the vDSO ELF image to find symbols
- Some JVM implementations that resolve time functions directly

**Mitigation:**
- Patch the vDSO in the process address space at runtime (overwrite vDSO function entries)
- Disable vDSO with `vdso=0` kernel boot parameter (performance cost: ~80ns per call)
- For containers: `docker run --sysctl kernel.vdso=0` (if supported)

### 4. io_uring — Kernel-Level Async I/O

**Severity: High (growing)**

`io_uring` (Linux 5.1+) provides a mechanism for asynchronous I/O that entirely bypasses libc. Operations are submitted to a submission queue (SQ) and completed via a completion queue (CQ), both shared between userspace and kernel via mmap.

```c
// io_uring bypasses ALL libc wrappers:
// - open, read, write, close
// - connect, accept, send, recv
// - stat, rename, unlink, mkdir
// All handled directly by the kernel without any libc function call

struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
io_uring_prep_openat(sqe, AT_FDCWD, "/etc/passwd", O_RDONLY, 0);
io_uring_submit(&ring);
// The open happens in the kernel — no libc function is ever called
```

**Affected operations via io_uring:**
- File I/O: `IORING_OP_OPENAT`, `IORING_OP_READ`, `IORING_OP_WRITE`, `IORING_OP_CLOSE`
- Network: `IORING_OP_CONNECT`, `IORING_OP_ACCEPT`, `IORING_OP_SEND`, `IORING_OP_RECV`
- Filesystem: `IORING_OP_STATX`, `IORING_OP_RENAMEAT`, `IORING_OP_UNLINKAT`, `IORING_OP_MKDIRAT`
- Advanced: `IORING_OP_SPLICE`, `IORING_OP_TEE`, `IORING_OP_SHUTDOWN`

**Mitigation:**
- Intercept `io_uring_setup()` and `io_uring_enter()` syscalls (but these are raw syscalls, which also bypass libc wrappers in some implementations)
- Block `io_uring` via seccomp: deny `SYS_io_uring_setup`, `SYS_io_uring_enter`, `SYS_io_uring_register`
- Use kernel-level interception (eBPF, LSM hooks)

### 5. Direct Assembly Syscalls

**Severity: Medium**

Any program can issue syscalls directly via assembly without going through libc:

```c
// x86-64 direct syscall
long result;
asm volatile(
    "syscall"
    : "=a" (result)
    : "a" (__NR_clock_gettime), "D" (CLOCK_REALTIME), "S" (&ts)
    : "rcx", "r11", "memory"
);

// Or via the syscall() wrapper (which is itself interceptable via LD_PRELOAD!)
#include <unistd.h>
#include <sys/syscall.h>
long result = syscall(SYS_clock_gettime, CLOCK_REALTIME, &ts);
```

**Note:** The `syscall()` libc wrapper function IS interceptable via `LD_PRELOAD`. But inline assembly `SYSCALL` instructions are not.

**Mitigation:** Intercept the `syscall()` libc wrapper to catch `syscall(SYS_*)` calls. For raw assembly syscalls, use seccomp or ptrace.

### 6. setuid/setgid Binaries

**Severity: Low (in containers)**

For security, the Linux dynamic linker ignores `LD_PRELOAD` for setuid and setgid binaries. This prevents privilege escalation attacks.

```bash
# setuid binary — LD_PRELOAD is silently ignored
-rwsr-xr-x 1 root root 12345 /usr/bin/passwd
LD_PRELOAD=./hook.so /usr/bin/passwd  # hook is ignored
```

**In containers:** This is rarely an issue because containers typically run as root (no need for setuid) or use user namespaces that strip setuid bits.

### 7. Internal glibc Calls Bypass PLT

**Severity: Medium**

glibc uses internal hidden aliases to make internal function calls without going through the PLT. This means:

- `fwrite()` calls `write()` internally via `__GI___libc_write`, not through the PLT
- `printf()` calls `write()` via internal paths
- `fopen()` calls `open()`/`openat()` via `__GI___open64`
- `getaddrinfo()` calls `socket()`, `connect()` internally

```
External call:  program → PLT → GOT → LD_PRELOAD hook → dlsym(RTLD_NEXT) → libc
Internal call:  libc fwrite() → __GI___libc_write() → syscall  (bypasses hook!)
```

**Practical impact:** If a program calls `write()` directly, your hook catches it. If a program calls `fwrite()`, your hook for `write()` does NOT catch the underlying write. You must also hook `fwrite()` separately.

**Mitigation:**
- Hook functions at every level (both `write()` and `fwrite()`, both `open()` and `fopen()`)
- Use `pmem/syscall_intercept` which patches syscall instructions inside libc
- Use ptrace or seccomp for completeness

### 8. dlopen() with RTLD_LOCAL

**Severity: Low**

Libraries loaded at runtime with `dlopen(path, RTLD_LOCAL)` have their own symbol scope. If such a library calls a function that your preloaded library interposes, the interposition works (the preloaded library is in the global scope). However, when your preloaded library uses `dlsym(RTLD_NEXT, ...)`, it may not find the symbol in the RTLD_LOCAL library's scope.

### 9. LD_PRELOAD Path Restrictions in Secure Mode

**Severity: Low (in containers)**

In secure-execution mode (triggered by setuid, setgid, or certain capabilities), the dynamic linker applies restrictions:
- `LD_PRELOAD` paths must not contain slashes (only library names)
- Libraries must be found in trusted directories (`/lib`, `/usr/lib`, etc.)
- Or the library must have the setuid bit set

### Summary of Bypass Vectors

| Bypass Vector | Severity | Prevalence | Mitigation |
|---|---|---|---|
| Static linking | Critical | Medium (Alpine, Rust, Go) | seccomp, ptrace |
| Go raw syscalls | Critical | High (any Go program) | seccomp, ptrace, hermit |
| vDSO | High | Universal (time functions) | vDSO patching, LD_PRELOAD usually suffices for most programs |
| io_uring | High | Growing (modern I/O) | Block via seccomp |
| Inline asm syscalls | Medium | Low (specialized code) | seccomp, ptrace |
| setuid binaries | Low | Low in containers | User namespaces |
| glibc internal calls | Medium | Universal | Hook at multiple levels |
| dlopen RTLD_LOCAL | Low | Rare | Careful symbol management |

---

## Performance Overhead

### Raw Interception Overhead

The `LD_PRELOAD` mechanism itself adds **minimal overhead per call**. The cost comes from PLT/GOT indirection, which involves:

1. **PLT stub:** An indirect jump instruction (~1-3 cycles if branch-predicted)
2. **GOT load:** A memory load from the GOT (~1 cycle if L1-cached)
3. **dlsym resolution:** One-time cost on first call (lazy binding) or at load time

**Estimated per-call overhead of the interception mechanism itself:** ~2-5 nanoseconds on modern hardware when the GOT entry is hot in L1 cache.

### Overhead From Interception Logic

The practical overhead is dominated by what the interceptor **does**, not the interception mechanism:

| Operation in Hook | Approximate Added Overhead |
|---|---|
| Just forwarding (pass-through) | ~2-5 ns |
| Reading a timestamp offset from thread-local storage | ~5-10 ns |
| Checking a condition and modifying result | ~5-15 ns |
| Logging to a ring buffer (lock-free) | ~20-50 ns |
| Logging to a file (with mutex) | ~100-1000 ns |
| Full event serialization and IPC | ~1-10 us |

### Benchmark Reference Points

**libfaketime overhead:** libfaketime adds approximately 50-200 ns per `clock_gettime()` call (depending on configuration and clock ID). Compared to a native `clock_gettime()` via vDSO (~20 ns), this is a ~3-10x slowdown for time calls specifically. For most applications, time calls are not the bottleneck, so this is negligible.

**Allocator interposition (TCMalloc, jemalloc):** These allocators are commonly deployed via `LD_PRELOAD` and actually **improve** performance vs. glibc's ptmalloc2:
- ptmalloc2: ~300 ns per malloc/free pair
- TCMalloc via LD_PRELOAD: ~50 ns per malloc/free pair
- The LD_PRELOAD mechanism adds negligible overhead compared to the allocator's own cost

**Semantic interposition (Python):** Fedora measured that disabling semantic interposition (`-fno-semantic-interposition`) in Python's build improved performance by 5-27%. This measures the cost of having PLT calls within libpython, not LD_PRELOAD specifically, but it demonstrates that PLT indirection has measurable cost when it affects hot paths in tight loops.

**syscall_intercept approach (hotpatching):** The `pmem/syscall_intercept` library, which hotpatches libc's syscall instructions, reports similar per-call overhead to LD_PRELOAD but catches all libc-internal syscalls as well.

### Overhead vs. Alternatives

| Mechanism | Per-call Overhead | Notes |
|---|---|---|
| LD_PRELOAD (pass-through) | ~2-5 ns | Userspace only, no kernel transition |
| LD_PRELOAD (with logic) | ~10-200 ns | Depends on hook complexity |
| seccomp-bpf (filter only) | ~50-100 ns | Kernel BPF evaluation |
| seccomp USER_NOTIF | ~2-5 us | Context switch to supervisor process |
| ptrace | ~5-20 us | Two context switches per syscall |
| gVisor sentry | ~1-5 us | Full syscall emulation |

**Key insight for LinBox:** LD_PRELOAD is **1000x faster** than ptrace and **100x faster** than seccomp USER_NOTIF per intercepted call. For a deterministic simulation that intercepts thousands of calls per second, this performance advantage is significant.

---

## Existing Mature LD_PRELOAD Projects

### libfaketime

**Repository:** [github.com/wolfcw/libfaketime](https://github.com/wolfcw/libfaketime)
**Maturity:** Very mature (2003+, actively maintained)
**Purpose:** Fake system time for a single application

**Intercepted functions:**
- `time()`, `gettimeofday()`, `clock_gettime()`
- `timespec_get()` (C11)
- With `FAKE_SLEEP`: `sleep()`, `nanosleep()`, `usleep()`, `alarm()`, `poll()`, `ppoll()`
- With `FAKE_TIMERS`: `timer_settime()`, `timer_gettime()`, `timerfd_settime()`, `timerfd_gettime()`
- `pthread_cond_timedwait()` (for proper timeout handling)
- Generic `syscall()` wrapper for selected syscall numbers

**Architecture:**
- Time offset configured via `FAKETIME` environment variable
- Supports absolute times (`"2024-01-01 00:00:00"`) and relative offsets (`"-1d"`)
- Supports time scaling (`FAKETIME_SPEED_FACTOR`)
- Handles all clock IDs including `CLOCK_MONOTONIC`, `CLOCK_BOOTTIME`
- Uses constructor for initialization, lazy resolution for function pointers
- Configuration can be changed at runtime via a file (`FAKETIME_TIMESTAMP_FILE`)

**Known issues:**
- JDK 21 + faketime can cause excessive CPU usage due to `pthread_cond_timedwait` interception creating inconsistent clock values
- Does not work with Go programs (raw syscalls)
- Does not work with statically linked programs
- Some Java VMs that use `dlopen()` for `librt` bypass faketime

**Relevance to LinBox:** Directly applicable. libfaketime's approach to time virtualization is the most battle-tested pattern for LD_PRELOAD-based time control.

### proxychains-ng

**Repository:** [github.com/rofl0r/proxychains-ng](https://github.com/rofl0r/proxychains-ng)
**Maturity:** Very mature (2003+, actively maintained fork)
**Purpose:** Redirect TCP connections through SOCKS/HTTP proxies

**Intercepted functions:**
- `connect()` — primary interception point, redirects to proxy
- `getaddrinfo()` — DNS resolution through proxy
- `gethostbyname()`, `gethostbyname_r()` — legacy DNS
- `close()` — track socket lifecycle

**Architecture:**
- Maintains a proxy chain configuration
- Intercepts `connect()` and performs SOCKS/HTTP CONNECT handshake with proxy
- DNS resolution redirected through the proxy (prevents DNS leaks)
- Supports SOCKS4, SOCKS5, and HTTP CONNECT proxies
- Chain modes: strict, dynamic, round-robin, random

**Limitations:**
- Does not work with Go programs (raw syscalls)
- Does not intercept UDP (SOCKS5 UDP association is not implemented in most versions)
- Does not intercept `sendto()` for connectionless protocols
- Programs using `dlopen()` for networking may bypass it

**Relevance to LinBox:** The pattern of intercepting `connect()` and redirecting to a simulation proxy is directly applicable for network virtualization.

### tsocks

**Repository:** Original tsocks + torsocks fork
**Maturity:** Mature (legacy, mostly superseded by proxychains-ng and torsocks)
**Purpose:** Transparent SOCKS proxying

**Intercepted functions:** `connect()`, `getaddrinfo()`, `gethostbyname()`

**Relevance:** Similar to proxychains but less actively maintained. The torsocks fork (used by Tor) is more current.

### libeatmydata

**Repository:** [github.com/stewartsmith/libeatmydata](https://github.com/stewartsmith/libeatmydata)
**Maturity:** Mature (2009+)
**Purpose:** Disable `fsync` and related calls to speed up I/O-heavy operations (e.g., database tests, package installation)

**Intercepted functions:**
- `fsync()` — returns 0 immediately (no-op)
- `fdatasync()` — returns 0 immediately
- `sync()` — returns 0 immediately
- `syncfs()` — returns 0 immediately
- `msync()` — returns 0 immediately
- `sync_file_range()` — returns 0 immediately
- `open()` / `openat()` — strips `O_SYNC`, `O_DSYNC` flags

**Architecture:**
- Extremely simple: each hooked function is a ~5-line wrapper that returns success without calling the real function
- The `open()` hook strips sync flags to prevent kernel-level sync behavior

**Relevance to LinBox:** Demonstrates how simple interceptors can dramatically change behavior. For simulation, similar patterns could be used to make I/O deterministic (e.g., fixed latency, ordered writes).

### fakeroot

**Repository:** [Debian fakeroot](https://salsa.debian.org/clint/fakeroot)
**Maturity:** Very mature (1997+, core Debian infrastructure)
**Purpose:** Simulate root privileges for package building

**Intercepted functions:**
- `chown()`, `fchown()`, `lchown()` — fake ownership changes
- `chmod()`, `fchmod()` — fake permission changes
- `stat()`, `fstat()`, `lstat()` — return faked ownership/permission info
- `mknod()`, `makedev()` — fake device creation
- `open()` — handle O_CREAT with faked permissions

**Architecture:**
- Uses a client-server model: a daemon (`faked`) maintains a database of fake file metadata
- The LD_PRELOAD library communicates with faked via Unix domain sockets
- File operations query/update faked, which tracks the "fake" uid, gid, mode for each inode
- Two modes: `fakeroot-sysv` (SysV IPC) and `fakeroot-tcp` (TCP sockets)

**Relevance to LinBox:** The client-server architecture is interesting — separating the interception (LD_PRELOAD library) from the state management (daemon) is a pattern that could work for LinBox's simulation controller.

### stderred

**Repository:** [github.com/ku1ik/stderred](https://github.com/ku1ik/stderred)
**Maturity:** Mature (2011+)
**Purpose:** Colorize stderr output in red

**Intercepted functions:**
- `write()` — check if fd == 2 (stderr), wrap with ANSI color codes
- `fwrite()` — same check for stream functions
- `fprintf()` — same check
- `error()` — GNU error function

**Architecture:**
- Checks if the file descriptor is 2 (stderr) and if the output is a terminal (via `isatty()`)
- Wraps stderr output with ANSI escape codes: `\033[31m` (red) before, `\033[0m` (reset) after
- Supports multi-architecture (32-bit and 64-bit) via `$LIB` DST token in LD_PRELOAD path
- Configurable blacklist via `STDERRED_BLACKLIST` environment variable (POSIX regex)

**Relevance to LinBox:** Simple example of fd-based interception. The pattern of checking fd number before modifying behavior is useful for `/dev/urandom` interception.

### Other Notable LD_PRELOAD Projects

| Project | Purpose | Intercepted Functions |
|---|---|---|
| **jemalloc** | High-performance allocator | `malloc`, `free`, `realloc`, `calloc`, `posix_memalign` |
| **tcmalloc** | Thread-caching allocator | Same as jemalloc |
| **electric-fence** | Memory debugging | `malloc`, `free`, `realloc` |
| **libsandbox** (Gentoo) | Filesystem sandboxing | `open`, `unlink`, `rename`, `mkdir`, etc. |
| **cwrap (uid_wrapper, nss_wrapper, socket_wrapper)** | Test wrappers for Samba | Various per wrapper |
| **libnss-wrapper** | Fake NSS (name service) | `getpwnam`, `getpwuid`, `getgrnam`, etc. |
| **libsocket_wrapper** | Redirect sockets to Unix domain sockets | `socket`, `connect`, `bind`, etc. |

---

## Comparison with Alternatives

### LD_PRELOAD vs. ptrace

| Aspect | LD_PRELOAD | ptrace |
|---|---|---|
| Overhead | ~2-5 ns per call | ~5-20 us per syscall (2 context switches) |
| Coverage | Only libc wrappers (PLT) | All syscalls (kernel-level) |
| Static binaries | No | Yes |
| Go programs | No | Yes |
| io_uring | No | Yes (intercepts io_uring_enter) |
| vDSO | Usually yes (via glibc wrapper) | No (vDSO runs in userspace) |
| Complexity | Low | High |
| Multi-threaded | Easy | Complex (one tracer per thread) |

### LD_PRELOAD vs. seccomp-bpf

| Aspect | LD_PRELOAD | seccomp-bpf |
|---|---|---|
| Overhead | ~2-5 ns | ~50-100 ns (BPF filter) |
| Coverage | Only libc wrappers | All syscalls |
| Modify arguments | Yes (full access) | No (filter only, cannot modify) |
| Modify return values | Yes | Yes (with SECCOMP_RET_TRAP handler) |
| Static binaries | No | Yes |
| Go programs | No | Yes |
| Unprivileged | Yes | Yes (with PR_SET_NO_NEW_PRIVS) |

### LD_PRELOAD vs. seccomp USER_NOTIF

| Aspect | LD_PRELOAD | seccomp USER_NOTIF |
|---|---|---|
| Overhead | ~2-5 ns | ~2-5 us (context switch to supervisor) |
| Coverage | Only libc wrappers | All syscalls |
| Modify arguments | Yes | Yes (via process_vm_readv/writev) |
| Modify return values | Yes | Yes |
| TOCTOU safety | Yes (same address space) | No (pointer args can be modified by other threads) |
| Architecture | In-process | Out-of-process supervisor |

### LD_PRELOAD vs. syscall_intercept

| Aspect | LD_PRELOAD | syscall_intercept |
|---|---|---|
| Mechanism | PLT/GOT symbol interposition | Hotpatch SYSCALL instructions in libc |
| Coverage | External calls to libc | All syscalls in libc (including internal) |
| Setup | LD_PRELOAD env var | LD_PRELOAD (but patches libc code at load) |
| Static binaries | No | No (needs libc to be dynamically loaded) |
| Stability | Very stable | Can break with libc updates |
| Dependency | libdl | capstone (disassembler) |

### LD_PRELOAD vs. eBPF

| Aspect | LD_PRELOAD | eBPF |
|---|---|---|
| Overhead | ~2-5 ns | ~50-200 ns (eBPF program execution) |
| Coverage | Only libc wrappers | All syscalls, kernel functions, hardware events |
| Modify behavior | Yes | Read-only (observe only, with some exceptions) |
| Privileges | None required | CAP_BPF or root |
| Complexity | Low | High |

---

## Implications for LinBox

### Recommended Hybrid Approach

For a deterministic simulation sandbox, a layered approach is optimal:

**Layer 1 — LD_PRELOAD (primary, low overhead):**
- Intercept time functions (`clock_gettime`, `gettimeofday`, `time`, sleep functions, timers)
- Intercept randomness (`getrandom`, `/dev/urandom` via `open`/`read`)
- Intercept network functions (`connect`, `accept`, `send`, `recv`, DNS)
- Intercept selected filesystem functions (for path virtualization, deterministic inode numbers)
- Intercept `syscall()` wrapper to catch `syscall(SYS_clock_gettime, ...)` patterns

**Layer 2 — seccomp-bpf (safety net, catches bypasses):**
- Block `io_uring` entirely (`SYS_io_uring_setup`, `SYS_io_uring_enter`, `SYS_io_uring_register`)
- Block raw `clock_gettime` syscalls that bypass libc (redirect to SECCOMP_RET_TRAP handler)
- Block `getrandom` syscall (force through libc wrapper)
- Enforce that all time/random/network syscalls go through the LD_PRELOAD layer

**Layer 3 — Container isolation (defense in depth):**
- Network namespaces for network isolation
- PID namespaces for process isolation
- Mount namespaces for filesystem isolation
- Block vDSO if needed (kernel parameter or runtime patching)

### What LD_PRELOAD Covers Well (for typical containerized services)

Most containerized applications (Node.js, Python, Java, Ruby, C/C++) use dynamic linking and call libc functions through the standard PLT mechanism. For these applications, LD_PRELOAD provides:

- **Complete time control** with ~50-200ns overhead per call
- **Network interception** for all TCP/UDP connections
- **Filesystem path virtualization** with minimal overhead
- **Deterministic randomness** by intercepting `getrandom` and `/dev/urandom`
- **No kernel privileges required** — works in unprivileged containers

### What LD_PRELOAD Cannot Cover

- Go programs (use seccomp-bpf as fallback)
- Statically linked programs (use seccomp-bpf as fallback)
- io_uring operations (block via seccomp)
- vDSO direct calls (usually not an issue — glibc wrapper interception suffices)
- glibc-internal call chains (hook at all levels: `fopen` AND `open`, `fwrite` AND `write`)

---

## Sources

### Technical References
- [ELF symbol interposition and RTLD_LOCAL](https://pernos.co/blog/interposition-rtld-local/)
- [The LD_PRELOAD trick](http://www.goldsborough.me/c/low-level/kernel/2016/08/29/16-48-53-the_-ld_preload-_trick/)
- [Correct usage of LD_PRELOAD for hooking libc functions](https://tbrindus.ca/correct-ld-preload-hooking-libc/)
- [Interposing internal libc calls](https://www.jimnewsome.net/posts/interposing-internal-libc-calls/)
- [All about Procedure Linkage Table](https://maskray.me/blog/2021-09-19-all-about-procedure-linkage-table)
- [GNU indirect function](https://maskray.me/blog/2021-01-18-gnu-indirect-function)

### vDSO
- [vdso(7) Linux manual page](https://man7.org/linux/man-pages/man7/vdso.7.html)
- [On Linux vDSO and clock_gettime](https://berthub.eu/articles/posts/on-linux-vdso-and-clockgettime/)
- [Cursing a process vDSO for time hacking](https://blog.davidv.dev/posts/cursing-a-process-vdso-for-time-hacking/)

### Limitations and Bypasses
- [Breaking LD_PRELOAD rootkit hooks with io_uring](https://matheuzsecurity.github.io/hacking/using-io-uring-to-break-linux-rootkits-hooks/)
- [Go issue 3744: syscall and LD_PRELOAD](https://github.com/golang/go/issues/3744)
- [How to write a rootkit without really trying](https://blog.trailofbits.com/2019/01/17/how-to-write-a-rootkit-without-really-trying/)

### Projects
- [libfaketime](https://github.com/wolfcw/libfaketime)
- [proxychains-ng](https://github.com/rofl0r/proxychains-ng)
- [stderred](https://github.com/ku1ik/stderred)
- [syscall_intercept](https://github.com/pmem/syscall_intercept)
- [awesome-ld-preload](https://github.com/gaul/awesome-ld-preload)

### Performance
- [Fedora Python no semantic interposition speedup](https://fedoraproject.org/wiki/Changes/PythonNoSemanticInterpositionSpeedup)
- [System calls are much slower on EC2](https://blog.packagecloud.io/system-calls-are-much-slower-on-ec2/)

### Alternatives
- [seccomp(2) manual page](https://man7.org/linux/man-pages/man2/seccomp.2.html)
- [Seccomp Notifier: New Frontiers in Unprivileged Container Development](https://people.kernel.org/brauner/the-seccomp-notifier-new-frontiers-in-unprivileged-container-development)
- [Intercepting and modifying Linux system calls with ptrace](https://notes.eatonphil.com/2023-10-01-intercepting-and-modifying-linux-system-calls-with-ptrace.html)
- [Redstone: Deterministic Simulation Testing Framework](https://www.scs.stanford.edu/24sp-cs244b/projects/Redstone_a_Deterministic_Simulation_Testing_Framework_for_Distributed_Systems.pdf)

---

## .so ↔ Sim Transport: IPC Options

The LD_PRELOAD `.so` runs inside a containerized process. It needs to communicate with the Sim process (Node.js/TypeScript) running outside the container. This section covers transport mechanisms, binary protocols, and optimization strategies.

### Architecture Overview

```
┌─────────────────────────────────────────────┐
│ Docker Container (LinBox)                   │
│                                             │
│   ┌──────────┐    LD_PRELOAD    ┌────────┐  │
│   │ App (Src) │ ──libc call──→  │  .so   │  │
│   │ Node/Py/C │ ←─result─────  │ hook   │  │
│   └──────────┘                  └───┬────┘  │
│                                     │       │
│                          IPC (socket/shm)   │
│                                     │       │
└─────────────────────────────────────┼───────┘
                                      │
                               ┌──────┴──────┐
                               │  Sim Process │
                               │  (Node.js)   │
                               └─────────────┘
```

The `.so` maps libc calls to LBP (LinBox Protocol) OBI hooks:

| libc call | OBI hook | Sim decides |
|---|---|---|
| `clock_gettime()`, `gettimeofday()`, `time()` | `time` | virtual timestamp |
| `getrandom()`, `read(/dev/urandom)` | `random` | deterministic value |
| `connect()`, `accept()`, `send()`, `recv()` | `network` | latency, failure |
| `open()`, `read()`, `write()` | `fs` | path virtualization |
| `getaddrinfo()` | `dns` | address override |

### Transport Option 1: Unix Domain Sockets

Unix domain sockets bypass the TCP/IP stack entirely. Communication goes through the kernel's socket buffer — no network serialization, no checksumming, no routing.

#### Latency Benchmarks

From [Kamal Marhubi's IPC measurements](https://kamalmarhubi.com/blog/2015/06/10/some-early-linux-ipc-latency-data/) (1M samples):

| Mechanism | p50 | p99 | p99.99 |
|---|---|---|---|
| **Unix domain socket** | **1.4 μs** | **1.9 μs** | **11.5 μs** |
| Pipe | 4.3 μs | 5.4 μs | 16.2 μs |
| eventfd | 4.4 μs | 5.1 μs | 14.6 μs |
| TCP loopback | 7.3 μs | 8.6 μs | 20.5 μs |

These are C-to-C numbers. With Node.js on the server side, expect ~10-50 μs due to event loop overhead.

#### Socket Types

| Feature | SOCK_STREAM | SOCK_DGRAM | SOCK_SEQPACKET |
|---|---|---|---|
| Connection-oriented | Yes | No | Yes |
| Message boundaries | No (byte stream) | Yes | Yes |
| Needs manual framing | **Yes** (length prefix) | No | No |
| Node.js `net` module | Yes | No | No |
| Persistent connection | Yes | N/A | Yes |

**SOCK_SEQPACKET** is ideal — preserves message boundaries (each `send()` = one `recv()`), reliable, ordered. But Node.js `net` module only supports SOCK_STREAM. Options:
- SOCK_STREAM + 4-byte length prefix (simplest, no native addon)
- SOCK_SEQPACKET via [node-unix-socket](https://github.com/bytedance/node-unix-socket) (ByteDance)

#### C Implementation (in the .so)

```c
#include <sys/socket.h>
#include <sys/un.h>

static __thread int sim_fd = -1;  // Per-thread for zero contention

static int sim_connect(void) {
    if (sim_fd >= 0) return sim_fd;

    sim_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sim_fd < 0) return -1;

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    const char *path = getenv("LINBOX_SOCK");
    if (!path) path = "/var/run/linbox/sim.sock";
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (connect(sim_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sim_fd);
        sim_fd = -1;
        return -1;
    }
    return sim_fd;
}

// Length-prefix framing for SOCK_STREAM
static int sim_request(const void *req, uint32_t req_len,
                       void *resp, uint32_t resp_max) {
    int fd = sim_connect();
    if (fd < 0) return -1;

    // Send: [4-byte LE length][payload]
    struct iovec iov[2] = {
        { .iov_base = &req_len, .iov_len = 4 },
        { .iov_base = (void*)req, .iov_len = req_len }
    };
    writev(fd, iov, 2);

    // Recv: [4-byte LE length][payload]
    uint32_t rlen;
    recv(fd, &rlen, 4, MSG_WAITALL);
    if (rlen > resp_max) return -1;
    return recv(fd, resp, rlen, MSG_WAITALL);
}
```

#### Node.js Server (Sim Side)

```typescript
import * as net from 'net';

const server = net.createServer((socket) => {
    socket.setNoDelay(true);
    let buf = Buffer.alloc(0);

    socket.on('data', (chunk) => {
        buf = Buffer.concat([buf, chunk]);
        while (buf.length >= 4) {
            const len = buf.readUInt32LE(0);
            if (buf.length < 4 + len) break;
            const msg = buf.subarray(4, 4 + len);
            buf = buf.subarray(4 + len);

            const response = handleHook(msg);

            const header = Buffer.alloc(4);
            header.writeUInt32LE(response.length, 0);
            socket.write(Buffer.concat([header, response]));
        }
    });
});

server.listen('/var/run/linbox/sim.sock');
```

#### Docker Integration

```yaml
services:
  sim:
    volumes:
      - linbox-sock:/var/run/linbox

  box:
    environment:
      - LD_PRELOAD=/usr/lib/linbox-hook.so
      - LINBOX_SOCK=/var/run/linbox/sim.sock
    volumes:
      - linbox-sock:/var/run/linbox

volumes:
  linbox-sock:
```

Abstract namespace sockets (`\0linbox-sim.sock`) are an alternative — no filesystem path, auto-cleanup on crash. But require shared network namespace between containers.

### Transport Option 2: Shared Memory

POSIX shared memory (`shm_open`/`mmap`) maps the same physical memory into both processes. Reads are a memory load — no syscall, no kernel involvement.

#### Latency

| Mechanism | Round-trip |
|---|---|
| Shared memory + spin-wait | **0.1-0.5 μs** |
| Shared memory + futex | **1-3 μs** |
| Unix domain socket | 2-10 μs |
| TCP loopback | 10-30 μs |

Shared memory is 5-50x faster than sockets for a request-response. But for one-directional reads (policies), it's effectively **free** — just a memory load (~1-5 ns).

#### Cross-Container Sharing

Docker containers have isolated `/dev/shm` by default. Options for sharing:

**Option A: tmpfs volume (recommended)**
```yaml
services:
  sim:
    volumes:
      - linbox-shm:/dev/shm/linbox
  box:
    volumes:
      - linbox-shm:/dev/shm/linbox:ro  # Read-only for safety

volumes:
  linbox-shm:
    driver: local
    driver_opts:
      type: tmpfs
      device: tmpfs
```

**Option B: `--ipc=container:sim`** — shares IPC namespace, but broader than needed.

#### Synchronization Across Containers

| Primitive | Works cross-container? | Latency |
|---|---|---|
| **futex** (in shared mmap) | Yes | ~1-3 μs |
| POSIX named semaphore | Yes (shared /dev/shm) | ~2-5 μs |
| pthread mutex (PROCESS_SHARED) | Yes (in shared mmap) | ~1-3 μs |
| eventfd | No (fd per-process) | N/A |

For **read-only policy pattern** (Sim writes, .so reads): no synchronization needed at all — use atomic writes + memory barriers.

#### Sequence Counter for Safe Reads

```c
// Sim writes (Node.js N-API addon):
atomic_store(&policies->seq, seq + 1);    // Odd = writing
__sync_synchronize();
// ... write policy fields ...
__sync_synchronize();
atomic_store(&policies->seq, seq + 2);    // Even = done

// .so reads:
uint64_t s1, s2;
struct time_policy local;
do {
    s1 = atomic_load(&policies->seq);
    if (s1 & 1) continue;  // Writer in progress
    __sync_synchronize();
    memcpy(&local, &policies->time, sizeof(local));
    __sync_synchronize();
    s2 = atomic_load(&policies->seq);
} while (s1 != s2);
// `local` is a consistent snapshot
```

#### Shared Memory from Node.js

**Option A: N-API addon (recommended for production)**
```c
// linbox-native addon
napi_value CreateShm(napi_env env, napi_callback_info info) {
    int fd = open(path, O_RDWR | O_CREAT, 0644);
    ftruncate(fd, size);
    void *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);

    napi_value buffer;
    napi_create_external_arraybuffer(env, ptr, size, finalize, NULL, &buffer);
    return buffer;
}
```

**Option B: `mmap-io` npm package** — works but not widely maintained.

**Option C: Direct `/dev/shm` file writes** — simplest, but no atomicity guarantee.

#### Failure Modes

| Failure | Impact | Mitigation |
|---|---|---|
| Sim crashes | Policies freeze at last value | Heartbeat field; .so falls back to pass-through |
| Container restarts | .so has stale mmap | MAP_SHARED on file — survives if volume persists |
| Torn reads | Inconsistent policy | Sequence counter pattern (above) |
| Volume removed | SIGBUS | .so catches SIGBUS, falls back to pass-through |

### Transport Option 3: Comparison and Recommendation

#### Side-by-Side

| Aspect | Unix Socket | Shared Memory | Hybrid (shm + socket) |
|---|---|---|---|
| Setup complexity | Low | Medium | Medium-High |
| C code in .so | ~50 lines | ~100 lines | ~150 lines |
| Node.js code | ~30 lines (net) | ~80 lines (addon) | ~100 lines |
| Latency (hot path) | 5-50 μs | **1-5 ns** | **1-5 ns** |
| Latency (cold path) | 5-50 μs | 1-3 μs (futex) | 5-50 μs |
| Debugging | Easy (strace) | Hard (memory dumps) | Mixed |
| Docker integration | Volume mount | tmpfs volume | Both |

#### Recommended: Three-Tier Hybrid

```
┌──────────────────────────────────────────────────────────┐
│ Tier 1: Shared Memory (policies, read-only from .so)     │
│                                                          │
│  ┌─────────────┐ ┌────────────┐ ┌──────────┐            │
│  │ Time Policy  │ │ Random     │ │ FS Maps  │            │
│  │ offset+speed │ │ PRNG seed  │ │ path tbl │            │
│  └─────────────┘ └────────────┘ └──────────┘            │
│                                                          │
│  Sim updates atomically. .so reads directly.             │
│  Latency: ~1-5 ns (memory load). ~90% of all calls.     │
└──────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────┐
│ Tier 2: Unix Socket (per-call decisions)                 │
│                                                          │
│  connect() to 10.0.0.5:5432 → ask Sim → allow + 50ms    │
│  open("/etc/secret") → ask Sim → deny                   │
│                                                          │
│  Used when policy says ASK_SIM.                          │
│  Latency: ~5-50 μs. ~9% of calls.                       │
└──────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────┐
│ Tier 3: Unix Socket (control plane)                      │
│                                                          │
│  .so → Sim: "hello, PID 42, ready"                      │
│  Sim → .so: "policies at /dev/shm/linbox/policies"      │
│                                                          │
│  Lifecycle, registration, policy location.               │
│  Latency: doesn't matter. ~1% of calls.                 │
└──────────────────────────────────────────────────────────┘
```

**Why this works:**
- Time (the hottest path, called thousands of times/sec) — resolved locally in ~5 ns
- Random (also hot) — local PRNG with Sim-provided seed, ~1-3 ns
- Network decisions (rare, already slow operations) — IPC acceptable at ~50 μs
- FS path mapping (cacheable) — read table from shared memory, ~5 ns

---

## Binary Protocol: Wire Format

Messages between `.so` and Sim (Tier 2/3) need a compact binary format. Given that messages are small (20-100 bytes) and volume is high, the choice matters.

### Protocol Options Comparison

| Format | Encode | Decode | C deps | Node.js | Schema evolution |
|---|---|---|---|---|---|
| **Raw structs** | **0 ns** | **0 ns** (C), ~10 ns (JS) | None | Buffer API | None |
| MessagePack | 100-300 ns | 100-300 ns | cmp (~800 LOC) | msgpackr | Flexible |
| FlatBuffers | 50-100 ns | ~5 ns | flatcc (build) | flatbuffers | Excellent |
| Protobuf (nanopb) | 100-200 ns | 100-200 ns | nanopb (~20KB) | protobufjs | Excellent |
| Cap'n Proto | 20-100 ns | ~10 ns | capnpc-c | capnp-ts | Excellent |

### Recommended: Raw Structs (Phase 1)

For LinBox, raw structs are ideal. Both .so and Sim run on the same machine (same endianness, same architecture). Messages are small and fixed-format.

```c
// Request: 32 bytes, naturally aligned
struct sbp_request {
    uint8_t  type;       // HOOK_TIME=1, HOOK_RANDOM=2, HOOK_NETWORK=3, ...
    uint8_t  phase;      // PRE=0, POST=1
    uint16_t flags;
    uint32_t seq;        // Sequence number for correlation
    uint64_t arg1;       // Hook-specific
    uint64_t arg2;
    uint64_t arg3;
};

// Response: 24 bytes, naturally aligned
struct sbp_response {
    uint8_t  status;     // OK=0, FAIL=1, SHORT_CIRCUIT=2, DELAY=3
    uint8_t  _pad[3];
    uint32_t seq;
    uint64_t result1;
    uint64_t result2;
};
```

**C side — zero overhead:**
```c
struct sbp_request req = {
    .type = HOOK_NETWORK,
    .phase = PRE,
    .seq = next_seq++,
    .arg1 = dest_ip,
    .arg2 = dest_port,
};
struct sbp_response resp;
sim_request(&req, sizeof(req), &resp, sizeof(resp));
```

**Node.js side — DataView reads:**
```javascript
socket.on('data', (buf) => {
    const type  = buf.readUInt8(0);
    const phase = buf.readUInt8(1);
    const seq   = buf.readUInt32LE(4);
    const arg1  = buf.readBigUInt64LE(8);
    const arg2  = buf.readBigUInt64LE(16);
    const arg3  = buf.readBigUInt64LE(24);
    // ...
});
```

### Evolution Path

```
Phase 1: Raw structs (32/24 bytes fixed)
  → Zero deps, zero overhead, prototype-ready

Phase 2: Add MessagePack for variable-length hooks (fs paths, DNS names)
  → cmp.h/cmp.c (~800 lines) in .so, msgpackr in Node.js
  → Use raw structs for hot path, MessagePack for variable data

Phase 3: FlatBuffers (if protocol formalization needed)
  → Schema evolution, multi-team/multi-language support
  → Likely never needed for LinBox
```

### Hybrid Framing

```c
struct sbp_header {
    uint8_t  type;
    uint8_t  encoding;   // 0 = raw struct follows, 1 = msgpack follows
    uint16_t length;     // Total payload length
};
```

Fixed hooks (time, random, network connect) use encoding=0 with raw structs. Variable hooks (fs with paths, DNS with hostnames) use encoding=1 with MessagePack.

---

## Optimization: Policy-Based Caching

The key insight from [libfaketime](https://github.com/wolfcw/libfaketime): **don't do IPC per call — push policies, apply locally**.

### How libfaketime Does It

libfaketime does **not** use IPC for every `clock_gettime()`. Instead:

1. Reads configuration from environment variables or shared memory
2. Computes fake time using a formula: `fake_time = real_time × speed + offset`
3. Each call: one real `clock_gettime()` + arithmetic = ~2-5 ns overhead

The shared memory region (`/dev/shm/faketime_shm_PID`) contains:
```c
struct ft_shared_s {
    double   tick_speed;    // Time speed multiplier
    time_t   start_time;    // Fake start timestamp
    int      should_update; // Flag: re-read config
};
```

An external controller writes to shared memory; the `.so` reads. No synchronization needed — atomic reads of aligned fields suffice.

### Policy Model for LinBox

Apply the same approach to all hook types:

**Time policy (local, no IPC):**
```c
struct time_policy {
    int64_t  offset_ns;    // virtual_time = real_time + offset
    double   speed;        // 0 = frozen, 1 = real-time, 2 = double speed
    uint64_t frozen_at_ns; // If speed=0, return this
};

int clock_gettime(clockid_t clk, struct timespec *tp) {
    if (policies->time.speed == 0.0) {
        tp->tv_sec  = policies->time.frozen_at_ns / 1000000000ULL;
        tp->tv_nsec = policies->time.frozen_at_ns % 1000000000ULL;
        return 0;
    }
    real_clock_gettime(clk, tp);
    int64_t ns = tp->tv_sec * 1000000000LL + tp->tv_nsec;
    ns = (int64_t)(ns * policies->time.speed) + policies->time.offset_ns;
    tp->tv_sec  = ns / 1000000000LL;
    tp->tv_nsec = ns % 1000000000LL;
    return 0;
}
```

**Random policy (local, no IPC):**
```c
struct random_policy {
    uint64_t seed;     // Sim-provided deterministic seed
};

// .so runs its own PRNG — xoshiro256** or similar
static __thread uint64_t prng_state;

ssize_t getrandom(void *buf, size_t len, unsigned int flags) {
    uint8_t *out = buf;
    for (size_t i = 0; i < len; i += 8) {
        prng_state ^= prng_state << 13;
        prng_state ^= prng_state >> 7;
        prng_state ^= prng_state << 17;
        size_t n = (len - i) < 8 ? (len - i) : 8;
        memcpy(out + i, &prng_state, n);
    }
    return (ssize_t)len;
}
```

**Network policy (mostly local, IPC for exceptions):**
```c
struct network_policy {
    enum { NET_ALLOW, NET_DENY, NET_ASK_SIM } default_action;
    uint32_t default_latency_us;
    uint32_t jitter_us;
    struct {
        uint32_t ip;
        uint16_t port;
        enum { NET_ALLOW, NET_DENY, NET_ASK_SIM } action;
    } exceptions[32];
};

int connect(int fd, const struct sockaddr *addr, socklen_t len) {
    // Check policy locally
    int action = lookup_policy(addr);
    if (action == NET_DENY)
        return errno = ECONNREFUSED, -1;
    if (action == NET_ALLOW) {
        int ret = real_connect(fd, addr, len);
        usleep(policies->network.default_latency_us);  // Simulated latency
        return ret;
    }
    // action == NET_ASK_SIM → IPC to Sim
    return sim_network_hook(fd, addr, len);
}
```

**FS policy (local path remapping):**
```c
struct fs_policy {
    struct {
        char from[256];
        char to[256];
    } mappings[16];
    uint32_t count;
};
```

### Expected Call Distribution

| Hook type | Resolution | Latency | % of calls |
|---|---|---|---|
| Time | Local (policy formula) | ~2-5 ns | ~60% |
| Random | Local (PRNG with seed) | ~1-3 ns | ~15% |
| FS paths | Local (mapping table) | ~10-50 ns | ~10% |
| DNS | Local (override table) | ~10-50 ns | ~5% |
| Network (default) | Local (policy check) | ~10-50 ns | ~8% |
| Network (exception) | IPC to Sim | ~5-50 μs | ~2% |

**Result:** ~98% of intercepted calls resolved locally with < 50 ns overhead. Only ~2% require IPC round-trip.

### Shared Memory Layout

```
┌──────────────────────────────────────┐  offset 0
│ Header                               │
│   magic: 0x4C494E42 ("LINB")        │
│   version: 1                         │
│   seq: uint64 (sequence counter)     │
│   heartbeat: uint64 (Sim timestamp)  │
├──────────────────────────────────────┤  offset 64
│ Time Policy (64 bytes)               │
│   offset_ns, speed, frozen_at_ns     │
├──────────────────────────────────────┤  offset 128
│ Random Policy (16 bytes)             │
│   seed                               │
├──────────────────────────────────────┤  offset 144
│ Network Policy (512 bytes)           │
│   default_action, latency, jitter    │
│   exceptions[32]                     │
├──────────────────────────────────────┤  offset 656
│ FS Policy (4K)                       │
│   path mappings                      │
├──────────────────────────────────────┤  offset 4752
│ DNS Policy (1K)                      │
│   hostname overrides                 │
└──────────────────────────────────────┘  ~6K total
```

The entire policy region fits in 1-2 memory pages. CPU cache-friendly — hot fields (time, random) are at the top.

### Docker Compose: Complete Setup

```yaml
services:
  sim:
    build: ./sim
    volumes:
      - linbox-shm:/dev/shm/linbox      # Shared memory for policies
      - linbox-sock:/var/run/linbox      # Unix socket for IPC

  box:
    build: ./app
    environment:
      - LD_PRELOAD=/usr/lib/linbox-hook.so
      - LINBOX_SHM=/dev/shm/linbox/policies
      - LINBOX_SOCK=/var/run/linbox/sim.sock
    volumes:
      - linbox-shm:/dev/shm/linbox:ro   # Read-only
      - linbox-sock:/var/run/linbox
    depends_on:
      - sim

volumes:
  linbox-shm:
    driver: local
    driver_opts:
      type: tmpfs
      device: tmpfs
  linbox-sock:
```

---

## LinBox Architecture Summary

LinBox = Linux container with LD_PRELOAD-based simulation hooks.

```
┌────────────────────────────────────────────────────────────┐
│ LinBox (Docker container)                                  │
│                                                            │
│  ENV LD_PRELOAD=/usr/lib/linbox-hook.so                   │
│  ENV LINBOX_SHM=/dev/shm/linbox/policies                  │
│  ENV LINBOX_SOCK=/var/run/linbox/sim.sock                  │
│                                                            │
│  ┌──────────────────────────────────────────────────────┐  │
│  │ linbox-hook.so                                       │  │
│  │                                                      │  │
│  │  Tier 1: Local policy resolution (~98% of calls)     │  │
│  │    mmap(/dev/shm/linbox/policies, PROT_READ)         │  │
│  │    time → formula, random → PRNG, fs → mapping       │  │
│  │                                                      │  │
│  │  Tier 2: IPC to Sim (~2% of calls)                   │  │
│  │    Unix socket → /var/run/linbox/sim.sock             │  │
│  │    network decisions, complex hooks                   │  │
│  │                                                      │  │
│  │  Tier 3: Fallback (Sim unreachable)                   │  │
│  │    dlsym(RTLD_NEXT, ...) → real libc function         │  │
│  └──────────────────────────────────────────────────────┘  │
│                                                            │
│  seccomp-bpf (safety net):                                 │
│    Block io_uring, redirect raw syscalls                   │
│                                                            │
│  ┌──────────────────────────────────────────────────────┐  │
│  │ App (Src) — unmodified application code              │  │
│  │ Calls libc normally, gets virtual values             │  │
│  └──────────────────────────────────────────────────────┘  │
└────────────────────────────────────────────────────────────┘
         ↕ shared memory (policies)
         ↕ unix socket (decisions)
┌────────────────────────────────────────────────────────────┐
│ Sim Process (Node.js)                                      │
│                                                            │
│  - Writes policies to shared memory                        │
│  - Handles IPC requests for per-call decisions             │
│  - Implements LBP hook semantics (pre/post phases)         │
│  - Maintains virtual domain state (SI interface)           │
└────────────────────────────────────────────────────────────┘
```

**Key properties:**
- **Transparent:** App doesn't know it's under simulation
- **No kernel modification:** Standard Linux kernel, standard Docker
- **Low overhead:** ~98% of hooks resolved locally in < 50 ns
- **Graceful degradation:** If Sim unavailable, .so falls back to real libc
- **Composable:** Multiple LinBoxes connect to the same Sim via Lab

---

## Transport Sources

### IPC Benchmarks
- [Kamal Marhubi: Early Linux IPC Latency Data](https://kamalmarhubi.com/blog/2015/06/10/some-early-linux-ipc-latency-data/)
- [rigtorp/ipc-bench](https://github.com/rigtorp/ipc-bench)
- [Baeldung: IPC Performance Comparison](https://www.baeldung.com/linux/ipc-performance-comparison)

### Unix Domain Sockets
- [unix(7) Linux manual page](https://man7.org/linux/man-pages/man7/unix.7.html)
- [ByteDance node-unix-socket (SOCK_SEQPACKET for Node.js)](https://github.com/bytedance/node-unix-socket)
- [NodeVibe: Unix Domain Sockets — 50% Lower Latency](https://nodevibe.substack.com/p/the-nodejs-developers-guide-to-unix)

### Shared Memory
- [shm_overview(7) Linux manual page](https://man7.org/linux/man-pages/man7/shm_overview.7.html)
- [futex(2) Linux manual page](https://man7.org/linux/man-pages/man2/futex.2.html)

### libfaketime Architecture
- [libfaketime source code](https://github.com/wolfcw/libfaketime)

### Deterministic Simulation Platforms
- [Antithesis — deterministic simulation via custom hypervisor](https://antithesis.com)
- [Hermit (Meta) — deterministic executor via ptrace](https://github.com/facebookexperimental/hermit)

---

[← Back](README.md)
