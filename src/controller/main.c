#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "common/sbp.h"
#include "time-manager.h"

#define LINBOX_DEFAULT_SOCK "/tmp/linbox.sock"
#define LINBOX_DEFAULT_SHM "/linbox-shm"

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig) {
    (void)sig;
    g_stop = 1;
}

static int setup_server_socket(const char *sock_path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);

    unlink(sock_path);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }

    if (listen(fd, 64) != 0) {
        close(fd);
        return -1;
    }

    return fd;
}

static int send_ack(int fd, uint16_t flags) {
    sbp_message_t ack = {
        .type = SBP_MSG_ACK,
        .version = SBP_VERSION,
        .flags = flags,
    };
    uint8_t frame[SBP_MAX_FRAME_SIZE];
    size_t out_len = 0;
    if (sbp_serialize_message(&ack, frame, sizeof(frame), &out_len) != SBP_OK) {
        return -1;
    }
    return (write(fd, frame, out_len) == (ssize_t)out_len) ? 0 : -1;
}

static int handle_client_message(int fd, linbox_time_manager_t *tm) {
    uint8_t frame[SBP_MAX_FRAME_SIZE];
    ssize_t n = read(fd, frame, sizeof(frame));
    if (n <= 0) {
        return -1;
    }

    sbp_message_t msg;
    int rc = sbp_deserialize_message(frame, (size_t)n, &msg);
    if (rc != SBP_OK) {
        return -1;
    }

    switch (msg.type) {
    case SBP_MSG_HELLO:
        return send_ack(fd, msg.flags);
    case SBP_MSG_SET_TIME: {
        struct timespec ts = {
            .tv_sec = (time_t)msg.payload.set_time.tv_sec,
            .tv_nsec = (long)msg.payload.set_time.tv_nsec,
        };
        linbox_time_manager_set_time(tm, &ts);
        return send_ack(fd, msg.flags);
    }
    case SBP_MSG_SET_SEED:
        linbox_time_manager_set_seed(tm, msg.payload.set_seed.seed);
        return send_ack(fd, msg.flags);
    case SBP_MSG_REGISTER_PROCESS:
        return send_ack(fd, msg.flags);
    case SBP_MSG_ACK:
    default:
        return -1;
    }
}

int main(void) {
    const char *sock_path = getenv("LINBOX_SOCK");
    if (!sock_path || sock_path[0] == '\0') {
        sock_path = LINBOX_DEFAULT_SOCK;
    }

    const char *shm_path = getenv("LINBOX_SHM");
    if (!shm_path || shm_path[0] == '\0') {
        shm_path = LINBOX_DEFAULT_SHM;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    linbox_time_manager_t tm;
    if (linbox_time_manager_open(&tm, shm_path) != 0) {
        perror("linbox-controller: shm init failed");
        return 1;
    }

    int listen_fd = setup_server_socket(sock_path);
    if (listen_fd < 0) {
        perror("linbox-controller: socket setup failed");
        linbox_time_manager_close(&tm, shm_path);
        return 1;
    }

    fprintf(stderr, "linbox-controller: listening on %s, shm=%s\n", sock_path, shm_path);

    int client_fd = -1;
    struct timespec last_tick;
    clock_gettime(CLOCK_MONOTONIC, &last_tick);

    while (!g_stop) {
        struct pollfd pfds[2];
        int nfds = 0;

        pfds[nfds].fd = listen_fd;
        pfds[nfds].events = POLLIN;
        nfds++;

        if (client_fd >= 0) {
            pfds[nfds].fd = client_fd;
            pfds[nfds].events = POLLIN | POLLHUP | POLLERR;
            nfds++;
        }

        int prc = poll(pfds, nfds, 10);
        if (prc < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        if (pfds[0].revents & POLLIN) {
            int fd = accept(listen_fd, NULL, NULL);
            if (fd >= 0) {
                if (client_fd >= 0) {
                    close(client_fd);
                }
                client_fd = fd;
            }
        }

        if (client_fd >= 0 && nfds > 1) {
            if (pfds[1].revents & (POLLERR | POLLHUP)) {
                close(client_fd);
                client_fd = -1;
            } else if (pfds[1].revents & POLLIN) {
                if (handle_client_message(client_fd, &tm) != 0) {
                    close(client_fd);
                    client_fd = -1;
                }
            }
        }

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        int64_t delta_ns = ((int64_t)(now.tv_sec - last_tick.tv_sec) * 1000000000LL) +
                           (now.tv_nsec - last_tick.tv_nsec);
        if (delta_ns > 0) {
            linbox_time_manager_tick(&tm, delta_ns);
            last_tick = now;
        }
    }

    if (client_fd >= 0) {
        close(client_fd);
    }
    close(listen_fd);
    unlink(sock_path);
    linbox_time_manager_close(&tm, shm_path);

    return 0;
}
