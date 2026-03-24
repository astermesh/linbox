#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <unistd.h>

#include "common/sbp.h"
#include "linbox.h"
#include "syscall-raw.h"

#define LINBOX_DEFAULT_SOCK "/tmp/linbox.sock"
#define LINBOX_DEFAULT_SHM "/linbox-shm"

static linbox_state_t g_linbox_state = {
    .initialized = false,
    .resolving = false,
    .controller_connected = false,
    .warned_fallback = false,
    .sigsys_installed = false,
    .seccomp_installed = false,
    .controller_fd = -1,
    .fake_base = {.tv_sec = LINBOX_FAKE_EPOCH_SEC, .tv_nsec = 0},
    .real_start_mono = {.tv_sec = 0, .tv_nsec = 0},
    .resolution = {.tv_sec = 0, .tv_nsec = 1000}, /* 1 microsecond */
    .shared_seed = 0,
    .effective_seed = 0,
    .prng = {.state = 0},
    .shm = {.fd = -1, .size = 0, .layout = NULL},
};

linbox_state_t *linbox_state(void) { return &g_linbox_state; }

static uint64_t linbox_effective_seed_for_pid(uint64_t seed) {
    return linbox_prng_derive_process_seed(seed, (uint32_t)getpid());
}

uint64_t linbox_prng_seed_value(void) {
    linbox_init_state();
    linbox_state_t *st = linbox_state();

    if (st->controller_connected && st->shm.layout) {
        uint64_t shared =
            atomic_load_explicit(&st->shm.layout->prng_seed, memory_order_acquire);
        if (shared != st->shared_seed) {
            st->shared_seed = shared;
            st->effective_seed = linbox_effective_seed_for_pid(shared);
        }
    }

    return st->effective_seed;
}

void linbox_random_reseed(uint64_t seed) {
    linbox_state_t *st = linbox_state();
    st->effective_seed = seed;
    linbox_prng_seed(&st->prng, seed);
}

ssize_t linbox_random_fill(void *buf, size_t len) {
    linbox_init_state();
    linbox_state_t *st = linbox_state();
    uint64_t seed = linbox_prng_seed_value();

    if (st->prng.state == 0 || st->effective_seed != seed) {
        linbox_random_reseed(seed);
    }

    linbox_prng_fill(&st->prng, buf, len);
    return (ssize_t)len;
}

uint32_t linbox_random_u32(void) {
    uint32_t out = 0;
    (void)linbox_random_fill(&out, sizeof(out));
    return out;
}

uint32_t linbox_random_uniform(uint32_t upper_bound) {
    linbox_init_state();
    linbox_state_t *st = linbox_state();
    uint64_t seed = linbox_prng_seed_value();

    if (st->prng.state == 0 || st->effective_seed != seed) {
        linbox_random_reseed(seed);
    }

    return linbox_prng_uniform(&st->prng, upper_bound);
}

static int linbox_connect_controller(const char *sock_path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }

    sbp_message_t hello = {
        .type = SBP_MSG_HELLO,
        .version = SBP_VERSION,
        .flags = 0,
    };

    uint8_t frame[SBP_MAX_FRAME_SIZE];
    size_t out_len = 0;
    if (sbp_serialize_message(&hello, frame, sizeof(frame), &out_len) != SBP_OK) {
        close(fd);
        return -1;
    }

    if (write(fd, frame, out_len) != (ssize_t)out_len) {
        close(fd);
        return -1;
    }

    ssize_t n = read(fd, frame, sizeof(frame));
    if (n <= 0) {
        close(fd);
        return -1;
    }

    sbp_message_t ack;
    if (sbp_deserialize_message(frame, (size_t)n, &ack) != SBP_OK || ack.type != SBP_MSG_ACK) {
        close(fd);
        return -1;
    }

    return fd;
}

void linbox_init_state(void) {
    if (g_linbox_state.initialized) {
        return;
    }

    (void)linbox_raw_syscall2(SYS_clock_gettime, CLOCK_MONOTONIC,
                              (long)&g_linbox_state.real_start_mono);

    const char *sock_path = getenv("LINBOX_SOCK");
    if (!sock_path || sock_path[0] == '\0') {
        sock_path = LINBOX_DEFAULT_SOCK;
    }

    const char *shm_path = getenv("LINBOX_SHM");
    if (!shm_path || shm_path[0] == '\0') {
        shm_path = LINBOX_DEFAULT_SHM;
    }

    g_linbox_state.controller_fd = linbox_connect_controller(sock_path);
    if (g_linbox_state.controller_fd >= 0 &&
        linbox_shm_attach(shm_path, &g_linbox_state.shm) == 0) {
        g_linbox_state.controller_connected = true;
    } else {
        if (g_linbox_state.controller_fd >= 0) {
            close(g_linbox_state.controller_fd);
            g_linbox_state.controller_fd = -1;
        }
        g_linbox_state.controller_connected = false;
    }

    g_linbox_state.shared_seed =
        g_linbox_state.controller_connected && g_linbox_state.shm.layout
            ? atomic_load_explicit(&g_linbox_state.shm.layout->prng_seed, memory_order_acquire)
            : 0;
    g_linbox_state.effective_seed = linbox_effective_seed_for_pid(g_linbox_state.shared_seed);
    linbox_prng_seed(&g_linbox_state.prng, g_linbox_state.effective_seed);

    g_linbox_state.initialized = true;
}

void linbox_reinit_state(void) {
    linbox_state_t *st = linbox_state();

    if (st->controller_fd >= 0) {
        close(st->controller_fd);
        st->controller_fd = -1;
    }
    st->controller_connected = false;
    st->initialized = false;
    st->prng.state = 0;
    st->effective_seed = 0;

    if (st->shm.fd >= 0) {
        linbox_shm_detach(&st->shm);
    }
    st->shm.fd = -1;
    st->shm.layout = NULL;
    st->shm.size = 0;

    linbox_init_state();
}

__attribute__((visibility("default"))) void linbox_noop(void) {
    /* Intentionally empty scaffold symbol. */
}

static int linbox_seccomp_enabled(void) {
    const char *disable = getenv("LINBOX_DISABLE_SECCOMP");
    return !(disable && strcmp(disable, "0") != 0);
}

__attribute__((constructor)) static void linbox_ctor(void) {
    linbox_init_state();

    if (linbox_seccomp_enabled()) {
        if (linbox_install_sigsys_handler() != 0) {
            (void)fprintf(stderr, "linbox: failed to install SIGSYS handler: %s\n",
                          strerror(errno));
        } else if (linbox_install_seccomp() != 0) {
            (void)fprintf(stderr, "linbox: failed to install seccomp filter: %s\n",
                          strerror(errno));
        }
    }

    (void)fprintf(stderr, "linbox: shim loaded (pid=%d)\n", getpid());
    if (!g_linbox_state.controller_connected) {
        (void)fprintf(stderr, "linbox: controller unavailable, fallback to real time\n");
    }
}
