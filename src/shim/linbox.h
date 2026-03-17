#ifndef LINBOX_SHIM_LINBOX_H
#define LINBOX_SHIM_LINBOX_H

#include <stdbool.h>
#include <time.h>

#include "common/shm-layout.h"

#define LINBOX_FAKE_EPOCH_SEC 1735689600LL /* 2025-01-01 00:00:00 UTC */

typedef struct linbox_state {
    bool initialized;
    bool resolving;
    bool controller_connected;
    bool warned_fallback;
    int controller_fd;
    struct timespec fake_base;
    struct timespec real_start_mono;
    struct timespec resolution;
    linbox_shm_handle_t shm;
} linbox_state_t;

linbox_state_t *linbox_state(void);
void linbox_init_state(void);

/* Testing helpers */
int linbox_virtual_clock_gettime(clockid_t clk_id, struct timespec *tp);
int linbox_virtual_clock_getres(clockid_t clk_id, struct timespec *res);
int linbox_real_clock_gettime_available(void);

#endif