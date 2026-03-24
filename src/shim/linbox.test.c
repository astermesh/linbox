#define _GNU_SOURCE

#include <criterion/criterion.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/times.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "common/shm-layout.h"
#include "linbox.h"

static void reset_state(void) {
    linbox_state_t *st = linbox_state();
    st->controller_connected = false;
    st->warned_fallback = false;
    st->sigsys_installed = false;
    st->seccomp_installed = false;
    if (st->shm.fd >= 0) {
        linbox_shm_detach(&st->shm);
    }
    st->shm.fd = -1;
    st->shm.layout = NULL;
}

Test(time_intercept, fallback_without_controller_is_realish_time) {
    reset_state();

    struct timespec real_now = {0};
    cr_assert_eq(syscall(SYS_clock_gettime, CLOCK_REALTIME, &real_now), 0);

    struct timespec ts = {0};
    cr_assert_eq(clock_gettime(CLOCK_REALTIME, &ts), 0);

    long long delta = llabs((long long)ts.tv_sec - (long long)real_now.tv_sec);
    cr_assert_leq(delta, 5, "fallback should be close to real time");
}

Test(time_intercept, reads_virtual_time_from_shared_memory) {
    reset_state();

    char shm_name[64] = {0};
    snprintf(shm_name, sizeof(shm_name), "/linbox-shim-test-%d", getpid());

    linbox_shm_handle_t h = {.fd = -1};
    cr_assert_eq(linbox_shm_create(shm_name, &h), 0);

    struct timespec fake = {.tv_sec = LINBOX_FAKE_EPOCH_SEC + 42, .tv_nsec = 777};
    linbox_shm_write_time(h.layout, &fake);

    linbox_state_t *st = linbox_state();
    cr_assert_eq(linbox_shm_attach(shm_name, &st->shm), 0);
    st->controller_connected = true;

    struct timespec ts = {0};
    cr_assert_eq(clock_gettime(CLOCK_REALTIME, &ts), 0);
    cr_assert_eq(ts.tv_sec, fake.tv_sec);
    cr_assert_eq(ts.tv_nsec, fake.tv_nsec);

    linbox_shm_detach(&st->shm);
    st->controller_connected = false;

    cr_assert_eq(linbox_shm_destroy(shm_name, &h), 0);
}

Test(time_intercept, clock_getres_consistent) {
    struct timespec baseline = {0};
    cr_assert_eq(clock_getres(CLOCK_REALTIME, &baseline), 0);

    struct timespec res = {0};
    cr_assert_eq(clock_getres(CLOCK_MONOTONIC, &res), 0);
    cr_assert_eq(res.tv_sec, baseline.tv_sec);
    cr_assert_eq(res.tv_nsec, baseline.tv_nsec);
}

Test(time_intercept, clock_and_times_are_available) {
    clock_t c = clock();
    cr_assert_neq(c, (clock_t)-1);

    struct tms t = {0};
    clock_t ticks = times(&t);
    cr_assert_neq(ticks, (clock_t)-1);
}

Test(time_intercept, timespec_get_time_utc) {
    struct timespec ts = {0};
    int base = timespec_get(&ts, TIME_UTC);
    cr_assert_eq(base, TIME_UTC);
}

Test(time_intercept, rtld_next_resolution_available) {
    cr_assert_eq(linbox_real_clock_gettime_available(), 1);
}

Test(time_intercept, libc_syscall_wrapper_intercepts_clock_gettime) {
    reset_state();

    char shm_name[64] = {0};
    snprintf(shm_name, sizeof(shm_name), "/linbox-syscall-test-%d", getpid());

    linbox_shm_handle_t h = {.fd = -1};
    cr_assert_eq(linbox_shm_create(shm_name, &h), 0);

    struct timespec fake = {.tv_sec = LINBOX_FAKE_EPOCH_SEC + 99, .tv_nsec = 1234};
    linbox_shm_write_time(h.layout, &fake);

    linbox_state_t *st = linbox_state();
    cr_assert_eq(linbox_shm_attach(shm_name, &st->shm), 0);
    st->controller_connected = true;

    struct timespec ts = {0};
    cr_assert_eq(syscall(SYS_clock_gettime, CLOCK_REALTIME, &ts), 0);
    cr_assert_eq(ts.tv_sec, fake.tv_sec);
    cr_assert_eq(ts.tv_nsec, fake.tv_nsec);

    linbox_shm_detach(&st->shm);
    st->controller_connected = false;
    cr_assert_eq(linbox_shm_destroy(shm_name, &h), 0);
}

Test(time_intercept, libc_syscall_wrapper_passes_through_write) {
    int pipefd[2] = {-1, -1};
    cr_assert_eq(pipe(pipefd), 0);

    const char payload[] = "ok";
    cr_assert_eq(syscall(SYS_write, pipefd[1], payload, strlen(payload)), (long)strlen(payload));

    char buf[8] = {0};
    cr_assert_eq(read(pipefd[0], buf, sizeof(buf)), (ssize_t)strlen(payload));
    cr_assert_eq(strncmp(buf, payload, strlen(payload)), 0);

    close(pipefd[0]);
    close(pipefd[1]);
}
