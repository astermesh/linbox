#include "time-manager.h"

#include <errno.h>
#include <string.h>

#include "shim/linbox.h"

static struct timespec ns_add(struct timespec in, int64_t delta_ns) {
    int64_t ns = ((int64_t)in.tv_sec * 1000000000LL) + in.tv_nsec + delta_ns;
    struct timespec out;
    out.tv_sec = (time_t)(ns / 1000000000LL);
    out.tv_nsec = (long)(ns % 1000000000LL);
    if (out.tv_nsec < 0) {
        out.tv_nsec += 1000000000L;
        out.tv_sec -= 1;
    }
    return out;
}

int linbox_time_manager_open(linbox_time_manager_t *tm, const char *shm_path) {
    if (!tm || !shm_path) {
        errno = EINVAL;
        return -1;
    }

    memset(tm, 0, sizeof(*tm));
    tm->shm.fd = -1;

    if (linbox_shm_create(shm_path, &tm->shm) != 0) {
        if (errno != EEXIST) {
            return -1;
        }
        if (linbox_shm_attach(shm_path, &tm->shm) != 0) {
            return -1;
        }
    }

    tm->current.tv_sec = LINBOX_FAKE_EPOCH_SEC;
    tm->current.tv_nsec = 0;
    tm->seed = 0;

    linbox_shm_write_time(tm->shm.layout, &tm->current);
    atomic_store_explicit(&tm->shm.layout->prng_seed, tm->seed, memory_order_release);

    return 0;
}

void linbox_time_manager_close(linbox_time_manager_t *tm, const char *shm_path) {
    if (!tm) {
        return;
    }

    if (tm->shm.fd >= 0) {
        (void)linbox_shm_destroy(shm_path, &tm->shm);
    }
}

void linbox_time_manager_tick(linbox_time_manager_t *tm, int64_t delta_ns) {
    if (!tm || !tm->shm.layout) {
        return;
    }

    tm->current = ns_add(tm->current, delta_ns);
    linbox_shm_write_time(tm->shm.layout, &tm->current);
}

void linbox_time_manager_set_time(linbox_time_manager_t *tm, const struct timespec *ts) {
    if (!tm || !tm->shm.layout || !ts) {
        return;
    }

    tm->current = *ts;
    linbox_shm_write_time(tm->shm.layout, &tm->current);
}

void linbox_time_manager_set_seed(linbox_time_manager_t *tm, uint64_t seed) {
    if (!tm || !tm->shm.layout) {
        return;
    }

    tm->seed = seed;
    atomic_store_explicit(&tm->shm.layout->prng_seed, seed, memory_order_release);
}
