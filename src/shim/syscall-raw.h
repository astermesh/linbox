#ifndef LINBOX_SHIM_SYSCALL_RAW_H
#define LINBOX_SHIM_SYSCALL_RAW_H

#include <stdint.h>

long linbox_raw_syscall0(long nr);
long linbox_raw_syscall1(long nr, long a1);
long linbox_raw_syscall2(long nr, long a1, long a2);
long linbox_raw_syscall3(long nr, long a1, long a2, long a3);
long linbox_raw_syscall4(long nr, long a1, long a2, long a3, long a4);
long linbox_raw_syscall5(long nr, long a1, long a2, long a3, long a4, long a5);
long linbox_raw_syscall6(long nr, long a1, long a2, long a3, long a4, long a5, long a6);

int linbox_syscall_result(long rc);

#endif