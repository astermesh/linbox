#define _GNU_SOURCE

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t g_usr1_count = 0;

static void usr1_handler(int signo) {
    if (signo == SIGUSR1) {
        g_usr1_count++;
    }
}

static long inline_syscall2(long nr, long a1, long a2) {
    long rc;
    __asm__ volatile("syscall" : "=a"(rc) : "a"(nr), "D"(a1), "S"(a2) : "rcx", "r11", "memory");
    return rc;
}

static long inline_syscall3(long nr, long a1, long a2, long a3) {
    long rc;
    __asm__ volatile("syscall"
                     : "=a"(rc)
                     : "a"(nr), "D"(a1), "S"(a2), "d"(a3)
                     : "rcx", "r11", "memory");
    return rc;
}

static int expect_eq_u64(const char *label, uint64_t got, uint64_t want) {
    if (got != want) {
        fprintf(stderr, "%s: got=%llu want=%llu\n", label, (unsigned long long)got,
                (unsigned long long)want);
        return -1;
    }
    return 0;
}

int main(void) {
    struct sigaction sa = {.sa_handler = usr1_handler};
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGUSR1, &sa, NULL) != 0) {
        perror("sigaction");
        return 1;
    }

    struct timespec ts_inline = {0};
    long rc = inline_syscall2(SYS_clock_gettime, CLOCK_REALTIME, (long)&ts_inline);
    if (rc != 0) {
        fprintf(stderr, "inline clock_gettime failed: %ld errno=%d\n", rc, errno);
        return 2;
    }
    if (expect_eq_u64("clock_gettime sec", (uint64_t)ts_inline.tv_sec, 1735689600ULL) != 0) {
        return 3;
    }

    struct timeval tv = {0};
    rc = syscall(SYS_gettimeofday, &tv, NULL);
    if (rc != 0) {
        fprintf(stderr, "syscall(gettimeofday) failed: %ld errno=%d\n", rc, errno);
        return 4;
    }
    if (expect_eq_u64("gettimeofday sec", (uint64_t)tv.tv_sec, 1735689600ULL) != 0) {
        return 5;
    }

    time_t now = 0;
    rc = inline_syscall2(SYS_time, (long)&now, 0);
    if (rc != 1735689600L || now != 1735689600L) {
        fprintf(stderr, "time failed: rc=%ld now=%lld errno=%d\n", rc, (long long)now, errno);
        return 6;
    }

    unsigned char a[16] = {0};
    unsigned char b[16] = {0};
    rc = inline_syscall3(SYS_getrandom, (long)a, sizeof(a), 0);
    if (rc != (long)sizeof(a)) {
        fprintf(stderr, "inline getrandom failed: %ld errno=%d\n", rc, errno);
        return 7;
    }
    rc = syscall(SYS_getrandom, b, sizeof(b), 0);
    if (rc != (long)sizeof(b)) {
        fprintf(stderr, "syscall(getrandom) failed: %ld errno=%d\n", rc, errno);
        return 8;
    }
    if (memcmp(a, b, sizeof(a)) == 0) {
        fprintf(stderr, "expected independent deterministic draws, got identical buffers\n");
        return 9;
    }

    const char msg[] = "seccomp-pass-through\n";
    rc = syscall(SYS_write, STDOUT_FILENO, msg, sizeof(msg) - 1);
    if (rc != (long)(sizeof(msg) - 1)) {
        fprintf(stderr, "write passthrough failed: %ld errno=%d\n", rc, errno);
        return 10;
    }

    if (raise(SIGUSR1) != 0 || g_usr1_count != 1) {
        fprintf(stderr, "SIGUSR1 handler broken count=%d errno=%d\n", g_usr1_count, errno);
        return 11;
    }


#ifdef SYS_io_uring_setup
    errno = 0;
    rc = syscall(SYS_io_uring_setup, 1, NULL);
    if (rc != -1 || errno != ENOSYS) {
        fprintf(stderr, "io_uring_setup expected ENOSYS rc=%ld errno=%d\n", rc, errno);
        return 14;
    }
#endif

    for (int i = 0; i < 10000; i++) {
        struct timespec loop_ts = {0};
        if (inline_syscall2(SYS_clock_gettime, CLOCK_REALTIME, (long)&loop_ts) != 0 ||
            loop_ts.tv_sec != 1735689600L) {
            fprintf(stderr, "stress clock_gettime failed at i=%d\n", i);
            return 15;
        }
    }

    return 0;
}
