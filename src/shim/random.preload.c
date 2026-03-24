#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <unistd.h>

static void print_hex(const unsigned char *buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        printf("%02x", buf[i]);
    }
    printf("\n");
}

int main(void) {
    unsigned char a[16] = {0};
    unsigned char b[16] = {0};
    unsigned char c[16] = {0};

    if (getrandom(a, sizeof(a), 0) != (ssize_t)sizeof(a)) {
        return 1;
    }
    if (getentropy(b, sizeof(b)) != 0) {
        return 2;
    }

    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        return 3;
    }
    if (read(fd, c, sizeof(c)) != (ssize_t)sizeof(c)) {
        return 4;
    }
    if (close(fd) != 0) {
        return 5;
    }

    uint32_t arc = arc4random();
    uint32_t uni = arc4random_uniform(100);

    srand(42);
    int r1 = rand();
    int r2 = rand();

    unsigned int rr_seed = 77;
    int rr = rand_r(&rr_seed);

    print_hex(a, sizeof(a));
    print_hex(b, sizeof(b));
    print_hex(c, sizeof(c));
    printf("arc=%u uni=%u rand=%d,%d rand_r=%d seed=%u\n", arc, uni, r1, r2, rr, rr_seed);

    return 0;
}
