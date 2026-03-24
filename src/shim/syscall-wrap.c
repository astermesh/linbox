#define _GNU_SOURCE

#include <stdarg.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "linbox.h"
#include "resolve.h"

typedef long (*real_syscall_fn)(long number, ...);

static real_syscall_fn g_real_syscall = NULL;

long syscall(long number, ...) {
    va_list ap;
    long a1 = 0;
    long a2 = 0;
    long a3 = 0;
    long a4 = 0;
    long a5 = 0;
    long a6 = 0;

    va_start(ap, number);
    a1 = va_arg(ap, long);
    a2 = va_arg(ap, long);
    a3 = va_arg(ap, long);
    a4 = va_arg(ap, long);
    a5 = va_arg(ap, long);
    a6 = va_arg(ap, long);
    va_end(ap);

    switch (number) {
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
        LINBOX_RESOLVE_NEXT(g_real_syscall, "syscall", real_syscall_fn);
        return g_real_syscall(number, a1, a2, a3, a4, a5, a6);
    }
}
