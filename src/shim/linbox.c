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

#define LINBOX_DEFAULT_SOCK "/tmp/linbox.sock"
#define LINBOX_DEFAULT_SHM "/linbox-shm"

static linbox_state_t g_linbox_state = {
    .initialized = false,
    .resolving = false,
    .controller_connected = false,
    .warned_fallback = false,
    .controller_fd = -1,
    .fake_base = {.tv_sec = LINBOX_FAKE_EPOCH_SEC, .tv_nsec = 0},
    .real_start_mono = {.tv_sec = 0, .tv_nsec = 0},
    .resolution = {.tv_sec = 0, .tv_nsec = 1000}, /* 1 microsecond */
    .shm = {.fd = -1, .size = 0, .layout = NULL},
};

linbox_state_t *linbox_state(void) { return &g_linbox_state; }

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

    (void)syscall(SYS_clock_gettime, CLOCK_MONOTONIC, &g_linbox_state.real_start_mono);

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

    g_linbox_state.initialized = true;
}

__attribute__((visibility("default"))) void linbox_noop(void) {
    /* Intentionally empty scaffold symbol. */
}

__attribute__((constructor)) static void linbox_ctor(void) {
    linbox_init_state();
    (void)fprintf(stderr, "linbox: shim loaded (pid=%d)\n", getpid());
    if (!g_linbox_state.controller_connected) {
        (void)fprintf(stderr, "linbox: controller unavailable, fallback to real time\n");
    }
}
