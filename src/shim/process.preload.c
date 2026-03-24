#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "common/shm-layout.h"
#include "shim/linbox.h"

extern char **environ;

static int read_all(int fd, void *buf, size_t len) {
    size_t offset = 0;
    while (offset < len) {
        ssize_t n = read(fd, (char *)buf + offset, len - offset);
        if (n <= 0) {
            return -1;
        }
        offset += (size_t)n;
    }
    return 0;
}

static int wait_ok(pid_t pid) {
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        return -1;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return -1;
    }
    return 0;
}

static int slot_has_pid(linbox_shm_layout_t *layout, pid_t pid) {
    for (size_t i = 0; i < LINBOX_PROCESS_SLOT_COUNT; i++) {
        if (layout->process_slots[i].pid == (uint32_t)pid) {
            return 1;
        }
    }
    return 0;
}

int main(void) {
    char sock[128];
    char shm[128];
    snprintf(sock, sizeof(sock), "/tmp/linbox-process-%d.sock", getpid());
    snprintf(shm, sizeof(shm), "/linbox-process-%d", getpid());

    setenv("LINBOX_SOCK", sock, 1);
    setenv("LINBOX_SHM", shm, 1);

    pid_t ctrl = fork();
    if (ctrl < 0) {
        return 10;
    }
    if (ctrl == 0) {
        execl("./linbox-controller", "linbox-controller", (char *)NULL);
        _exit(127);
    }

    usleep(200000);

    linbox_shm_handle_t handle = {.fd = -1};
    if (linbox_shm_attach(shm, &handle) != 0) {
        kill(ctrl, SIGTERM);
        waitpid(ctrl, NULL, 0);
        return 11;
    }

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        kill(ctrl, SIGTERM);
        waitpid(ctrl, NULL, 0);
        return 12;
    }

    struct {
        uint64_t random_word;
        time_t sec;
        pid_t pid;
    } child_report = {0};

    pid_t child = fork();
    if (child < 0) {
        kill(ctrl, SIGTERM);
        waitpid(ctrl, NULL, 0);
        return 13;
    }

    if (child == 0) {
        close(pipefd[0]);
        struct timespec ts = {0};
        uint64_t word = 0;
        if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
            _exit(1);
        }
        if (getrandom(&word, sizeof(word), 0) != (ssize_t)sizeof(word)) {
            _exit(2);
        }
        child_report.random_word = word;
        child_report.sec = ts.tv_sec;
        child_report.pid = getpid();
        if (write(pipefd[1], &child_report, sizeof(child_report)) != (ssize_t)sizeof(child_report)) {
            _exit(3);
        }
        close(pipefd[1]);
        usleep(150000);
        _exit(0);
    }

    close(pipefd[1]);
    struct timespec parent_ts = {0};
    uint64_t parent_word = 0;
    if (clock_gettime(CLOCK_REALTIME, &parent_ts) != 0) {
        kill(ctrl, SIGTERM);
        waitpid(ctrl, NULL, 0);
        return 14;
    }
    if (getrandom(&parent_word, sizeof(parent_word), 0) != (ssize_t)sizeof(parent_word)) {
        kill(ctrl, SIGTERM);
        waitpid(ctrl, NULL, 0);
        return 15;
    }
    if (read_all(pipefd[0], &child_report, sizeof(child_report)) != 0) {
        kill(ctrl, SIGTERM);
        waitpid(ctrl, NULL, 0);
        return 16;
    }
    close(pipefd[0]);
    usleep(50000);
    if (parent_ts.tv_sec < LINBOX_FAKE_EPOCH_SEC || child_report.sec < LINBOX_FAKE_EPOCH_SEC) {
        kill(ctrl, SIGTERM);
        waitpid(ctrl, NULL, 0);
        return 18;
    }
    if (parent_word == child_report.random_word) {
        kill(ctrl, SIGTERM);
        waitpid(ctrl, NULL, 0);
        return 19;
    }
    if (!slot_has_pid(handle.layout, child_report.pid)) {
        kill(ctrl, SIGTERM);
        waitpid(ctrl, NULL, 0);
        return 20;
    }
    if (wait_ok(child) != 0) {
        kill(ctrl, SIGTERM);
        waitpid(ctrl, NULL, 0);
        return 17;
    }

    linbox_shm_detach(&handle);
    kill(ctrl, SIGTERM);
    waitpid(ctrl, NULL, 0);
    return 0;
}
