#define _GNU_SOURCE

#include <criterion/criterion.h>

#include <pthread.h>
#include <stdio.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "shm-layout.h"

typedef struct reader_ctx {
    const linbox_shm_layout_t *layout;
    atomic_int failures;
    atomic_int stop;
} reader_ctx_t;

static void *reader_thread(void *arg) {
    reader_ctx_t *ctx = (reader_ctx_t *)arg;
    while (!atomic_load(&ctx->stop)) {
        struct timespec ts = {0};
        if (linbox_shm_read_time(ctx->layout, &ts) != 0) {
            atomic_fetch_add(&ctx->failures, 1);
            continue;
        }
        if (ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000L) {
            atomic_fetch_add(&ctx->failures, 1);
        }
        if ((ts.tv_sec % 1000000000LL) != ts.tv_nsec) {
            atomic_fetch_add(&ctx->failures, 1);
        }
    }
    return NULL;
}

Test(shm, cross_process_seqlock_read_write) {
    char path[128];
    snprintf(path, sizeof(path), "/linbox-shm-test-%d", getpid());

    linbox_shm_handle_t h = {.fd = -1};
    cr_assert_eq(linbox_shm_create(path, &h), 0);

    struct timespec t = {.tv_sec = 1735689600, .tv_nsec = 123456789};
    linbox_shm_write_time(h.layout, &t);

    pid_t child = fork();
    cr_assert_neq(child, -1);

    if (child == 0) {
        linbox_shm_handle_t r = {.fd = -1};
        if (linbox_shm_attach(path, &r) != 0) {
            _exit(1);
        }
        struct timespec got = {0};
        if (linbox_shm_read_time(r.layout, &got) != 0) {
            _exit(2);
        }
        linbox_shm_detach(&r);
        if (got.tv_sec != t.tv_sec || got.tv_nsec != t.tv_nsec) {
            _exit(3);
        }
        _exit(0);
    }

    int status = 0;
    waitpid(child, &status, 0);
    cr_assert(WIFEXITED(status));
    cr_assert_eq(WEXITSTATUS(status), 0);

    cr_assert_eq(linbox_shm_destroy(path, &h), 0);
}

Test(shm, concurrent_reads_no_torn_values) {
    char path[128];
    snprintf(path, sizeof(path), "/linbox-shm-race-%d", getpid());

    linbox_shm_handle_t h = {.fd = -1};
    cr_assert_eq(linbox_shm_create(path, &h), 0);

    reader_ctx_t ctx = {
        .layout = h.layout,
        .failures = 0,
        .stop = 0,
    };

    enum { READERS = 4 };
    pthread_t readers[READERS];

    for (int i = 0; i < READERS; i++) {
        cr_assert_eq(pthread_create(&readers[i], NULL, reader_thread, &ctx), 0);
    }

    for (int i = 0; i < 10000; i++) {
        struct timespec ts = {
            .tv_sec = i,
            .tv_nsec = i % 1000000000,
        };
        linbox_shm_write_time(h.layout, &ts);
        if ((i % 64) == 0) {
            sched_yield();
        }
    }

    atomic_store(&ctx.stop, 1);
    for (int i = 0; i < READERS; i++) {
        pthread_join(readers[i], NULL);
    }

    cr_assert_eq(atomic_load(&ctx.failures), 0, "detected torn/inconsistent reads");

    cr_assert_eq(linbox_shm_destroy(path, &h), 0);
}
