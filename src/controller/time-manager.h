#ifndef LINBOX_CONTROLLER_TIME_MANAGER_H
#define LINBOX_CONTROLLER_TIME_MANAGER_H

#include <stdint.h>
#include <time.h>

#include "common/shm-layout.h"

typedef struct linbox_time_manager {
    linbox_shm_handle_t shm;
    struct timespec current;
    uint64_t seed;
} linbox_time_manager_t;

int linbox_time_manager_open(linbox_time_manager_t *tm, const char *shm_path);
void linbox_time_manager_close(linbox_time_manager_t *tm, const char *shm_path);
void linbox_time_manager_tick(linbox_time_manager_t *tm, int64_t delta_ns);
void linbox_time_manager_set_time(linbox_time_manager_t *tm, const struct timespec *ts);
void linbox_time_manager_set_seed(linbox_time_manager_t *tm, uint64_t seed);

#endif