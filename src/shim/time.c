#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/times.h>
#include <time.h>
#include <unistd.h>

#include "linbox.h"
#include "resolve.h"

typedef int (*real_clock_gettime_fn)(clockid_t, struct timespec *);
typedef int (*real_gettimeofday_fn)(struct timeval *, void *);
typedef time_t (*real_time_fn)(time_t *);
typedef int (*real_clock_getres_fn)(clockid_t, struct timespec *);
typedef clock_t (*real_clock_fn)(void);
typedef clock_t (*real_times_fn)(struct tms *);
typedef int (*real_timespec_get_fn)(struct timespec *, int);

static real_clock_gettime_fn g_real_clock_gettime = NULL;
static real_gettimeofday_fn g_real_gettimeofday = NULL;
static real_time_fn g_real_time = NULL;
static real_clock_getres_fn g_real_clock_getres = NULL;
static real_clock_fn g_real_clock = NULL;
static real_times_fn g_real_times = NULL;
static real_timespec_get_fn g_real_timespec_get = NULL;

static int linbox_fallback_now(struct timespec *tp) {
    if (g_real_clock_gettime) {
        return g_real_clock_gettime(CLOCK_REALTIME, tp);
    }
    return (int)syscall(SYS_clock_gettime, CLOCK_REALTIME, tp);
}

static int linbox_virtual_now(struct timespec *tp) {
    linbox_init_state();

    linbox_state_t *st = linbox_state();

    if (st->controller_connected && st->shm.layout) {
        if (linbox_shm_read_time(st->shm.layout, tp) == 0) {
            return 0;
        }
        st->controller_connected = false;
    }

    return linbox_fallback_now(tp);
}

int linbox_virtual_clock_gettime(clockid_t clk_id, struct timespec *tp) {
    if (!tp) {
        errno = EINVAL;
        return -1;
    }

    switch (clk_id) {
    case CLOCK_REALTIME:
    case CLOCK_REALTIME_COARSE:
    case CLOCK_MONOTONIC:
    case CLOCK_MONOTONIC_COARSE:
#ifdef CLOCK_MONOTONIC_RAW
    case CLOCK_MONOTONIC_RAW:
#endif
#ifdef CLOCK_BOOTTIME
    case CLOCK_BOOTTIME:
#endif
    case CLOCK_PROCESS_CPUTIME_ID:
    case CLOCK_THREAD_CPUTIME_ID:
        return linbox_virtual_now(tp);
    default:
        errno = EINVAL;
        return -1;
    }
}

int linbox_virtual_clock_getres(clockid_t clk_id, struct timespec *res) {
    if (!res) {
        errno = EINVAL;
        return -1;
    }

    (void)clk_id;
    linbox_init_state();
    *res = linbox_state()->resolution;
    return 0;
}

int linbox_real_clock_gettime_available(void) {
    LINBOX_RESOLVE_NEXT(g_real_clock_gettime, "clock_gettime", real_clock_gettime_fn);
    return g_real_clock_gettime != NULL;
}

int clock_gettime(clockid_t clk_id, struct timespec *tp) {
    LINBOX_RESOLVE_NEXT(g_real_clock_gettime, "clock_gettime", real_clock_gettime_fn);
    return linbox_virtual_clock_gettime(clk_id, tp);
}

int gettimeofday(struct timeval *tv, void *tz) {
    (void)tz;
    if (!tv) {
        errno = EINVAL;
        return -1;
    }

    struct timespec ts;
    if (linbox_virtual_now(&ts) != 0) {
        return -1;
    }

    tv->tv_sec = ts.tv_sec;
    tv->tv_usec = (suseconds_t)(ts.tv_nsec / 1000);
    return 0;
}

time_t time(time_t *tloc) {
    struct timespec ts;
    if (linbox_virtual_now(&ts) != 0) {
        return (time_t)-1;
    }

    if (tloc) {
        *tloc = ts.tv_sec;
    }
    return ts.tv_sec;
}

int clock_getres(clockid_t clk_id, struct timespec *res) {
    return linbox_virtual_clock_getres(clk_id, res);
}

clock_t clock(void) {
    struct timespec ts;
    if (linbox_virtual_now(&ts) != 0) {
        return (clock_t)-1;
    }

    int64_t usec = ((int64_t)ts.tv_sec * 1000000LL) + (ts.tv_nsec / 1000);
    return (clock_t)((usec * CLOCKS_PER_SEC) / 1000000LL);
}

clock_t times(struct tms *buf) {
    struct timespec ts;
    if (linbox_virtual_now(&ts) != 0) {
        return (clock_t)-1;
    }

    long hz = sysconf(_SC_CLK_TCK);
    if (hz <= 0) {
        errno = EINVAL;
        return (clock_t)-1;
    }

    clock_t ticks =
        (clock_t)(((int64_t)ts.tv_sec * hz) + ((int64_t)ts.tv_nsec * hz) / 1000000000LL);

    if (buf) {
        memset(buf, 0, sizeof(*buf));
        buf->tms_utime = ticks;
    }

    return ticks;
}

int timespec_get(struct timespec *ts, int base) {
    if (!ts || base != TIME_UTC) {
        return 0;
    }

    if (linbox_virtual_now(ts) != 0) {
        return 0;
    }

    return base;
}
