#include "shm-layout.h"

#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static uint64_t now_ns(void) {
    struct timespec ts = {0};
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
}

void linbox_shm_init_layout(linbox_shm_layout_t *layout) {
    if (!layout) {
        return;
    }

    memset(layout, 0, sizeof(*layout));

    layout->header.magic = LINBOX_SHM_MAGIC;
    layout->header.version = LINBOX_SHM_VERSION;
    layout->header.header_size = (uint16_t)sizeof(linbox_shm_header_t);
    atomic_store_explicit(&layout->header.global_seq, 0, memory_order_relaxed);
    atomic_store_explicit(&layout->header.heartbeat_ns, now_ns(), memory_order_relaxed);

    size_t base = offsetof(linbox_shm_layout_t, policy_area);
    layout->header.network_policy_offset = (uint32_t)base;
    layout->header.filesystem_policy_offset = (uint32_t)(base + (LINBOX_POLICY_AREA_SIZE / 4));
    layout->header.dns_policy_offset = (uint32_t)(base + (LINBOX_POLICY_AREA_SIZE / 2));
    layout->header.policy_area_size = LINBOX_POLICY_AREA_SIZE;

    atomic_store_explicit(&layout->time_seq, 0, memory_order_relaxed);
    layout->virtual_time.tv_sec = 0;
    layout->virtual_time.tv_nsec = 0;
    atomic_store_explicit(&layout->prng_seed, 0, memory_order_relaxed);
    atomic_store_explicit(&layout->flags, 0, memory_order_relaxed);
}

static int linbox_shm_open_map(const char *path, int oflags, mode_t mode, int needs_init,
                               linbox_shm_handle_t *out) {
    if (!path || !out) {
        return -1;
    }

    int fd = shm_open(path, oflags, mode);
    if (fd < 0) {
        return -1;
    }

    size_t map_size = sizeof(linbox_shm_layout_t);
    if (needs_init) {
        if (ftruncate(fd, (off_t)map_size) != 0) {
            close(fd);
            return -1;
        }
    }

    void *mem = mmap(NULL, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mem == MAP_FAILED) {
        close(fd);
        return -1;
    }

    out->fd = fd;
    out->size = map_size;
    out->layout = (linbox_shm_layout_t *)mem;

    if (needs_init) {
        linbox_shm_init_layout(out->layout);
    }

    return 0;
}

int linbox_shm_create(const char *path, linbox_shm_handle_t *out) {
    return linbox_shm_open_map(path, O_CREAT | O_EXCL | O_RDWR, 0600, 1, out);
}

int linbox_shm_attach(const char *path, linbox_shm_handle_t *out) {
    if (linbox_shm_open_map(path, O_RDWR, 0600, 0, out) != 0) {
        return -1;
    }

    if (out->layout->header.magic != LINBOX_SHM_MAGIC) {
        linbox_shm_detach(out);
        errno = EPROTO;
        return -1;
    }

    return 0;
}

void linbox_shm_detach(linbox_shm_handle_t *handle) {
    if (!handle) {
        return;
    }

    if (handle->layout && handle->layout != MAP_FAILED) {
        munmap(handle->layout, handle->size);
    }

    if (handle->fd >= 0) {
        close(handle->fd);
    }

    handle->layout = NULL;
    handle->fd = -1;
    handle->size = 0;
}

int linbox_shm_destroy(const char *path, linbox_shm_handle_t *handle) {
    if (handle) {
        linbox_shm_detach(handle);
    }

    if (!path) {
        errno = EINVAL;
        return -1;
    }

    return shm_unlink(path);
}

void linbox_shm_write_time(linbox_shm_layout_t *layout, const struct timespec *ts) {
    if (!layout || !ts) {
        return;
    }

    uint32_t seq = atomic_load_explicit(&layout->time_seq, memory_order_relaxed);
    atomic_store_explicit(&layout->time_seq, seq + 1, memory_order_release); /* odd */

    layout->virtual_time = *ts;

    atomic_store_explicit(&layout->time_seq, seq + 2, memory_order_release); /* even */
    atomic_store_explicit(&layout->header.heartbeat_ns, now_ns(), memory_order_relaxed);
}

int linbox_shm_read_time(const linbox_shm_layout_t *layout, struct timespec *out) {
    if (!layout || !out) {
        errno = EINVAL;
        return -1;
    }

    for (;;) {
        uint32_t seq1 = atomic_load_explicit(&layout->time_seq, memory_order_acquire);
        if (seq1 & 1u) {
            sched_yield();
            continue;
        }

        struct timespec snapshot = layout->virtual_time;

        uint32_t seq2 = atomic_load_explicit(&layout->time_seq, memory_order_acquire);
        if (seq1 == seq2 && !(seq2 & 1u)) {
            *out = snapshot;
            return 0;
        }
    }
}
