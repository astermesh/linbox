#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    char with_preload[128] = {0};
    if (run_and_read("date -u '+%a %b %e %T %Z %Y'", with_preload, sizeof(with_preload)) != 0) {
        return 1;
    }

    if (strncmp(with_preload, "Wed Jan  1 00:00:00 UTC 2025", 28) != 0) {
        return 2;
    }

    unsetenv("LD_PRELOAD");

    char without_preload[128] = {0};
    if (run_and_read("date -u '+%a %b %e %T %Z %Y'", without_preload, sizeof(without_preload)) !=
        0) {
        return 3;
    }

    if (strncmp(without_preload, "Wed Jan  1 00:00:00 UTC 2025", 28) == 0) {
        return 4;
    }

    return 0;
}
