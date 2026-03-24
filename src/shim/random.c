#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "linbox.h"
#include "prng.h"
#include "resolve.h"

typedef int (*real_open_fn)(const char *, int, ...);
typedef int (*real_openat_fn)(int, const char *, int, ...);
typedef ssize_t (*real_read_fn)(int, void *, size_t);
typedef int (*real_close_fn)(int);

typedef struct linbox_virtual_fd {
    int fd;
    int used;
} linbox_virtual_fd_t;

static real_open_fn g_real_open = NULL;
static real_openat_fn g_real_openat = NULL;
static real_read_fn g_real_read = NULL;
static real_close_fn g_real_close = NULL;

static linbox_virtual_fd_t g_virtual_fds[16];
static int g_rand_seeded = 0;

static int linbox_is_random_path(const char *path) {
    return path && ((strcmp(path, "/dev/urandom") == 0) || (strcmp(path, "/dev/random") == 0));
}

static int linbox_allocate_virtual_fd(void) {
    for (size_t i = 0; i < (sizeof(g_virtual_fds) / sizeof(g_virtual_fds[0])); i++) {
        if (!g_virtual_fds[i].used) {
            int fd = (int)(LINBOX_VIRTUAL_FD_BASE + i);
            g_virtual_fds[i].fd = fd;
            g_virtual_fds[i].used = 1;
            return fd;
        }
    }
    errno = EMFILE;
    return -1;
}

static int linbox_is_virtual_fd(int fd) {
    for (size_t i = 0; i < (sizeof(g_virtual_fds) / sizeof(g_virtual_fds[0])); i++) {
        if (g_virtual_fds[i].used && g_virtual_fds[i].fd == fd) {
            return 1;
        }
    }
    return 0;
}

static void linbox_release_virtual_fd(int fd) {
    for (size_t i = 0; i < (sizeof(g_virtual_fds) / sizeof(g_virtual_fds[0])); i++) {
        if (g_virtual_fds[i].used && g_virtual_fds[i].fd == fd) {
            g_virtual_fds[i].used = 0;
            g_virtual_fds[i].fd = -1;
            return;
        }
    }
}

static void linbox_seed_rand_once(void) {
    if (!g_rand_seeded) {
        srand((unsigned int)linbox_prng_seed_value());
        srandom((unsigned int)linbox_prng_seed_value());
        g_rand_seeded = 1;
    }
}

ssize_t linbox_virtual_getrandom(void *buf, size_t buflen, unsigned int flags) {
    (void)flags;
    return linbox_random_fill(buf, buflen);
}

ssize_t getrandom(void *buf, size_t buflen, unsigned int flags) {
    return linbox_virtual_getrandom(buf, buflen, flags);
}

int getentropy(void *buf, size_t buflen) {
    if (buflen > 256) {
        errno = EIO;
        return -1;
    }
    return (linbox_random_fill(buf, buflen) >= 0) ? 0 : -1;
}

uint32_t arc4random(void) { return linbox_random_u32(); }

void arc4random_buf(void *buf, size_t nbytes) { (void)linbox_random_fill(buf, nbytes); }

uint32_t arc4random_uniform(uint32_t upper_bound) { return linbox_random_uniform(upper_bound); }

int rand(void) {
    linbox_seed_rand_once();
    return (int)(linbox_random_u32() & RAND_MAX);
}

void srand(unsigned int seed) {
    linbox_random_reseed((uint64_t)seed);
    g_rand_seeded = 1;
}

long int random(void) {
    linbox_seed_rand_once();
    return (long int)(linbox_random_u32() & 0x7FFFFFFFu);
}

void srandom(unsigned int seed) {
    linbox_random_reseed((uint64_t)seed);
    g_rand_seeded = 1;
}

int rand_r(unsigned int *seedp) {
    if (!seedp) {
        errno = EINVAL;
        return -1;
    }

    linbox_prng_t prng;
    linbox_prng_seed(&prng, (uint64_t)(*seedp));
    uint32_t next = linbox_prng_next_u32(&prng);
    *seedp = next;
    return (int)(next & RAND_MAX);
}

int open(const char *path, int flags, ...) {
    if (linbox_is_random_path(path)) {
        return linbox_allocate_virtual_fd();
    }

    LINBOX_RESOLVE_NEXT(g_real_open, "open", real_open_fn);
    return g_real_open(path, flags, 0);
}

int openat(int dirfd, const char *path, int flags, ...) {
    if (linbox_is_random_path(path)) {
        return linbox_allocate_virtual_fd();
    }

    LINBOX_RESOLVE_NEXT(g_real_openat, "openat", real_openat_fn);
    return g_real_openat(dirfd, path, flags, 0);
}

ssize_t read(int fd, void *buf, size_t count) {
    if (linbox_is_virtual_fd(fd)) {
        return linbox_random_fill(buf, count);
    }

    LINBOX_RESOLVE_NEXT(g_real_read, "read", real_read_fn);
    return g_real_read(fd, buf, count);
}

int close(int fd) {
    if (linbox_is_virtual_fd(fd)) {
        linbox_release_virtual_fd(fd);
        return 0;
    }

    LINBOX_RESOLVE_NEXT(g_real_close, "close", real_close_fn);
    return g_real_close(fd);
}
