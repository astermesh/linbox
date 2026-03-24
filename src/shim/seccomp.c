#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#include <sys/syscall.h>

#include "linbox.h"

#ifndef SECCOMP_RET_ERRNO
#define SECCOMP_RET_ERRNO 0x00050000U
#endif

#define LINBOX_ALLOW SECCOMP_RET_ALLOW
#define LINBOX_TRAP SECCOMP_RET_TRAP
#define LINBOX_ERRNO(x) (SECCOMP_RET_ERRNO | ((x)&SECCOMP_RET_DATA))

#define SC_ALLOW(nr)                                                                               \
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (nr), 0, 1), BPF_STMT(BPF_RET | BPF_K, LINBOX_ALLOW)
#define SC_TRAP(nr)                                                                                \
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (nr), 0, 1), BPF_STMT(BPF_RET | BPF_K, LINBOX_TRAP)
#define SC_ERRNO(nr, err)                                                                          \
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (nr), 0, 1), BPF_STMT(BPF_RET | BPF_K, LINBOX_ERRNO(err))

int linbox_install_seccomp(void) {
    struct sock_filter filter[] = {
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, (uint32_t)offsetof(struct seccomp_data, nr)),
        SC_TRAP(__NR_clock_gettime),
#ifdef __NR_gettimeofday
        SC_TRAP(__NR_gettimeofday),
#endif
        SC_TRAP(__NR_time),
        SC_TRAP(__NR_getrandom),
#ifdef __NR_io_uring_setup
        SC_ERRNO(__NR_io_uring_setup, ENOSYS),
#endif
#ifdef __NR_io_uring_enter
        SC_ERRNO(__NR_io_uring_enter, ENOSYS),
#endif
#ifdef __NR_io_uring_register
        SC_ERRNO(__NR_io_uring_register, ENOSYS),
#endif
        BPF_STMT(BPF_RET | BPF_K, LINBOX_ALLOW),
    };

    struct sock_fprog prog = {
        .len = (unsigned short)(sizeof(filter) / sizeof(filter[0])),
        .filter = filter,
    };

    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        return -1;
    }

    if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog) != 0) {
        return -1;
    }

    linbox_state()->seccomp_installed = true;
    return 0;
}
