#define _GNU_SOURCE

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int run_and_read(const char *cmd, char *buf, size_t n) {
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        return -1;
    }

    if (!fgets(buf, (int)n, fp)) {
        pclose(fp);
        return -1;
    }

    int status = pclose(fp);
    return status == 0 ? 0 : -1;
}

int main(void) {
    char sock[128];
    char shm[128];
    snprintf(sock, sizeof(sock), "/tmp/linbox-preload-%d.sock", getpid());
    snprintf(shm, sizeof(shm), "/linbox-preload-%d", getpid());

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

    usleep(150000);

    char with_preload[128] = {0};
    if (run_and_read("LC_ALL=C TZ=UTC date '+%a %b %e %T %Z %Y'", with_preload,
                     sizeof(with_preload)) != 0) {
        kill(ctrl, SIGTERM);
        waitpid(ctrl, NULL, 0);
        return 1;
    }

    if (strncmp(with_preload, "Wed Jan  1 00:00:00 UTC 2025", 28) != 0) {
        kill(ctrl, SIGTERM);
        waitpid(ctrl, NULL, 0);
        return 2;
    }

    unsetenv("LD_PRELOAD");

    char without_preload[128] = {0};
    if (run_and_read("LC_ALL=C TZ=UTC date '+%a %b %e %T %Z %Y'", without_preload,
                     sizeof(without_preload)) != 0) {
        kill(ctrl, SIGTERM);
        waitpid(ctrl, NULL, 0);
        return 3;
    }

    kill(ctrl, SIGTERM);
    waitpid(ctrl, NULL, 0);

    if (strncmp(without_preload, "Wed Jan  1 00:00:00 UTC 2025", 28) == 0) {
        return 4;
    }

    return 0;
}
