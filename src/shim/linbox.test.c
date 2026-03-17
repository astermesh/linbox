#define _GNU_SOURCE

#include <criterion/criterion.h>
#include <sys/times.h>
#include <sys/time.h>
#include <time.h>

#include "linbox.h"

static void assert_virtual_2025(const struct timespec *ts) {
    cr_assert_not_null(ts);
    cr_assert_eq(ts->tv_sec, LINBOX_FAKE_EPOCH_SEC, "expected virtual epoch %lld, got %lld",
                 (long long)LINBOX_FAKE_EPOCH_SEC, (long long)ts->tv_sec);
}

Test(time_intercept, clock_gettime_realtime) {
    struct timespec ts = {0};
    cr_assert_eq(clock_gettime(CLOCK_REALTIME, &ts), 0);
    assert_virtual_2025(&ts);
}

Test(time_intercept, all_required_clock_ids) {
    const clockid_t ids[] = {
        CLOCK_REALTIME,           CLOCK_REALTIME_COARSE,   CLOCK_MONOTONIC, CLOCK_MONOTONIC_COARSE,
#ifdef CLOCK_MONOTONIC_RAW
        CLOCK_MONOTONIC_RAW,
#endif
#ifdef CLOCK_BOOTTIME
        CLOCK_BOOTTIME,
#endif
        CLOCK_PROCESS_CPUTIME_ID, CLOCK_THREAD_CPUTIME_ID,
    };

    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        struct timespec ts = {0};
        cr_assert_eq(clock_gettime(ids[i], &ts), 0, "clock id %d failed", (int)ids[i]);
        assert_virtual_2025(&ts);
    }
}

Test(time_intercept, gettimeofday_fake_time) {
    struct timeval tv = {0};
    cr_assert_eq(gettimeofday(&tv, NULL), 0);
    cr_assert_eq(tv.tv_sec, LINBOX_FAKE_EPOCH_SEC);
}

Test(time_intercept, time_returns_fake_epoch) {
    time_t out = 0;
    time_t ret = time(&out);
    cr_assert_eq(ret, LINBOX_FAKE_EPOCH_SEC);
    cr_assert_eq(out, LINBOX_FAKE_EPOCH_SEC);
}

Test(time_intercept, clock_getres_consistent) {
    const clockid_t ids[] = {
        CLOCK_REALTIME,           CLOCK_REALTIME_COARSE,   CLOCK_MONOTONIC, CLOCK_MONOTONIC_COARSE,
#ifdef CLOCK_MONOTONIC_RAW
        CLOCK_MONOTONIC_RAW,
#endif
#ifdef CLOCK_BOOTTIME
        CLOCK_BOOTTIME,
#endif
        CLOCK_PROCESS_CPUTIME_ID, CLOCK_THREAD_CPUTIME_ID,
    };

    struct timespec baseline = {0};
    cr_assert_eq(clock_getres(ids[0], &baseline), 0);

    for (size_t i = 1; i < sizeof(ids) / sizeof(ids[0]); i++) {
        struct timespec res = {0};
        cr_assert_eq(clock_getres(ids[i], &res), 0);
        cr_assert_eq(res.tv_sec, baseline.tv_sec);
        cr_assert_eq(res.tv_nsec, baseline.tv_nsec);
    }
}

Test(time_intercept, clock_and_times_virtual_cpu_time) {
    clock_t c = clock();
    cr_assert_neq(c, (clock_t)-1);

    struct tms t = {0};
    clock_t ticks = times(&t);
    cr_assert_neq(ticks, (clock_t)-1);
    cr_assert_eq(t.tms_utime, ticks);
}

Test(time_intercept, timespec_get_time_utc) {
    struct timespec ts = {0};
    int base = timespec_get(&ts, TIME_UTC);
    cr_assert_eq(base, TIME_UTC);
    assert_virtual_2025(&ts);
}

Test(time_intercept, rtld_next_resolution_available) {
    cr_assert_eq(linbox_real_clock_gettime_available(), 1);
}
