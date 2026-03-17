#include <stdio.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "linbox.h"

static linbox_state_t g_linbox_state = {
    .initialized = false,
    .resolving = false,
    .fake_base = {.tv_sec = LINBOX_FAKE_EPOCH_SEC, .tv_nsec = 0},
    .real_start_mono = {.tv_sec = 0, .tv_nsec = 0},
    .resolution = {.tv_sec = 0, .tv_nsec = 1000}, /* 1 microsecond */
};

linbox_state_t *linbox_state(void) { return &g_linbox_state; }

void linbox_init_state(void) {
    if (g_linbox_state.initialized) {
        return;
    }

    (void)syscall(SYS_clock_gettime, CLOCK_MONOTONIC, &g_linbox_state.real_start_mono);
    g_linbox_state.initialized = true;
}

__attribute__((visibility("default"))) void linbox_noop(void) {
    /* Intentionally empty scaffold symbol. */
}

__attribute__((constructor)) static void linbox_ctor(void) {
    linbox_init_state();
    (void)fprintf(stderr, "linbox: shim loaded (pid=%d)\n", getpid());
}
