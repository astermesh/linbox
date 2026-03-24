#include "syscall-raw.h"

#include <errno.h>

#if !defined(__x86_64__)
#error "linbox raw syscall helpers currently support x86_64 only"
#endif

long linbox_raw_syscall0(long nr) {
    long rc;
    __asm__ volatile("syscall" : "=a"(rc) : "a"(nr) : "rcx", "r11", "memory");
    return rc;
}

long linbox_raw_syscall1(long nr, long a1) {
    long rc;
    __asm__ volatile("syscall" : "=a"(rc) : "a"(nr), "D"(a1) : "rcx", "r11", "memory");
    return rc;
}

long linbox_raw_syscall2(long nr, long a1, long a2) {
    long rc;
    __asm__ volatile("syscall" : "=a"(rc) : "a"(nr), "D"(a1), "S"(a2) : "rcx", "r11", "memory");
    return rc;
}

long linbox_raw_syscall3(long nr, long a1, long a2, long a3) {
    long rc;
    __asm__ volatile("syscall"
                     : "=a"(rc)
                     : "a"(nr), "D"(a1), "S"(a2), "d"(a3)
                     : "rcx", "r11", "memory");
    return rc;
}

long linbox_raw_syscall4(long nr, long a1, long a2, long a3, long a4) {
    long rc;
    register long r10 __asm__("r10") = a4;
    __asm__ volatile("syscall"
                     : "=a"(rc)
                     : "a"(nr), "D"(a1), "S"(a2), "d"(a3), "r"(r10)
                     : "rcx", "r11", "memory");
    return rc;
}

long linbox_raw_syscall5(long nr, long a1, long a2, long a3, long a4, long a5) {
    long rc;
    register long r10 __asm__("r10") = a4;
    register long r8 __asm__("r8") = a5;
    __asm__ volatile("syscall"
                     : "=a"(rc)
                     : "a"(nr), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8)
                     : "rcx", "r11", "memory");
    return rc;
}

long linbox_raw_syscall6(long nr, long a1, long a2, long a3, long a4, long a5, long a6) {
    long rc;
    register long r10 __asm__("r10") = a4;
    register long r8 __asm__("r8") = a5;
    register long r9 __asm__("r9") = a6;
    __asm__ volatile("syscall"
                     : "=a"(rc)
                     : "a"(nr), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
                     : "rcx", "r11", "memory");
    return rc;
}

int linbox_syscall_result(long rc) {
    if (rc < 0 && rc >= -4095) {
        errno = (int)-rc;
        return -1;
    }

    return (int)rc;
}
