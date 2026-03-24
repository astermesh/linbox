#ifndef LINBOX_SHIM_LINBOX_H
#define LINBOX_SHIM_LINBOX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>

#include "common/shm-layout.h"
#include "prng.h"

#define LINBOX_FAKE_EPOCH_SEC 1735689600LL /* 2025-01-01 00:00:00 UTC */
#define LINBOX_VIRTUAL_FD_BASE 1000000

typedef struct linbox_state {
    bool initialized;
    bool resolving;
    bool controller_connected;
    bool warned_fallback;
    bool sigsys_installed;
    bool seccomp_installed;
    int controller_fd;
    struct timespec fake_base;
    struct timespec real_start_mono;
    struct timespec resolution;
    uint64_t shared_seed;
    uint64_t effective_seed;
    linbox_prng_t prng;
    linbox_shm_handle_t shm;
} linbox_state_t;

linbox_state_t *linbox_state(void);
void linbox_init_state(void);
void linbox_reinit_state(void);

/* Testing helpers */
int linbox_virtual_clock_gettime(clockid_t clk_id, struct timespec *tp);
int linbox_virtual_gettimeofday(struct timeval *tv, void *tz);
time_t linbox_virtual_time(time_t *tloc);
ssize_t linbox_virtual_getrandom(void *buf, size_t buflen, unsigned int flags);
int linbox_virtual_clock_getres(clockid_t clk_id, struct timespec *res);
int linbox_real_clock_gettime_available(void);
int linbox_install_sigsys_handler(void);
int linbox_install_seccomp(void);

uint64_t linbox_prng_seed_value(void);
void linbox_random_reseed(uint64_t seed);
ssize_t linbox_random_fill(void *buf, size_t len);
uint32_t linbox_random_u32(void);
uint32_t linbox_random_uniform(uint32_t upper_bound);

#endif