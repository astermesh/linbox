#define _GNU_SOURCE

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <ucontext.h>

#ifndef SYS_SECCOMP
#define SYS_SECCOMP 1
#endif

#include "linbox.h"
#include "syscall-raw.h"

#if !defined(__x86_64__)
#error "linbox SIGSYS handler currently supports x86_64 only"
#endif

static long linbox_dispatch_syscall(long nr, long a1, long a2, long a3, long a4, long a5, long a6) {
    (void)a4;
    (void)a5;
    (void)a6;

    switch (nr) {
    case __NR_clock_gettime:
        return linbox_virtual_clock_gettime((clockid_t)a1, (struct timespec *)a2);
#ifdef __NR_gettimeofday
    case __NR_gettimeofday:
        return linbox_virtual_gettimeofday((struct timeval *)a1, (void *)a2);
#endif
    case __NR_time:
        return (long)linbox_virtual_time((time_t *)a1);
    case __NR_getrandom:
        return linbox_virtual_getrandom((void *)a1, (size_t)a2, (unsigned int)a3);
    default:
        return linbox_raw_syscall6(nr, a1, a2, a3, a4, a5, a6);
    }
}

static void linbox_sigsys_handler(int signo, siginfo_t *info, void *context) {
    (void)signo;

    if (!info || info->si_code != SYS_SECCOMP || !context) {
        return;
    }

    ucontext_t *uc = (ucontext_t *)context;
    greg_t *gregs = uc->uc_mcontext.gregs;

    long nr = info->si_syscall;
    long rc = linbox_dispatch_syscall(nr, (long)gregs[REG_RDI], (long)gregs[REG_RSI],
                                      (long)gregs[REG_RDX], (long)gregs[REG_R10],
                                      (long)gregs[REG_R8], (long)gregs[REG_R9]);

    if (rc < 0 && rc >= -4095) {
        rc = -errno;
    }

    gregs[REG_RAX] = (greg_t)rc;
}

int linbox_install_sigsys_handler(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = linbox_sigsys_handler;
    sa.sa_flags = SA_SIGINFO | SA_NODEFER;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGSYS, &sa, NULL) != 0) {
        return -1;
    }

    linbox_state()->sigsys_installed = true;
    return 0;
}
