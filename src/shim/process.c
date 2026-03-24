#define _GNU_SOURCE

#include <errno.h>
#include <linux/sched.h>
#include <sched.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include "common/sbp.h"
#include "linbox.h"
#include "resolve.h"
#include "syscall-raw.h"

extern char **environ;

typedef int (*real_execve_fn)(const char *, char *const[], char *const[]);
typedef int (*real_execvpe_fn)(const char *, char *const[], char *const[]);
typedef int (*real_execvp_fn)(const char *, char *const[]);
typedef int (*real_posix_spawn_fn)(pid_t *, const char *, const posix_spawn_file_actions_t *,
                                   const posix_spawnattr_t *, char *const[], char *const[]);
typedef int (*real_posix_spawnp_fn)(pid_t *, const char *, const posix_spawn_file_actions_t *,
                                    const posix_spawnattr_t *, char *const[], char *const[]);
typedef int (*real_clone_fn)(int (*)(void *), void *, int, void *, ...);
typedef pid_t (*real_fork_fn)(void);

static real_execve_fn g_real_execve = NULL;
static real_execvpe_fn g_real_execvpe = NULL;
static real_execvp_fn g_real_execvp = NULL;
static real_posix_spawn_fn g_real_posix_spawn = NULL;
static real_posix_spawnp_fn g_real_posix_spawnp = NULL;
static real_clone_fn g_real_clone = NULL;
static real_fork_fn g_real_fork = NULL;

typedef struct linbox_env_binding {
    const char *name;
    const char *value;
} linbox_env_binding_t;

static char **linbox_inject_runtime_env(char *const envp[]) {
    linbox_env_binding_t needed[] = {
        {.name = "LD_PRELOAD", .value = getenv("LD_PRELOAD")},
        {.name = "LINBOX_SOCK", .value = getenv("LINBOX_SOCK")},
        {.name = "LINBOX_SHM", .value = getenv("LINBOX_SHM")},
        {.name = "LINBOX_DISABLE_SECCOMP", .value = getenv("LINBOX_DISABLE_SECCOMP")},
    };

    size_t count = 0;
    if (envp) {
        while (envp[count]) {
            count++;
        }
    }

    int missing = 0;
    for (size_t i = 0; i < sizeof(needed) / sizeof(needed[0]); i++) {
        if (!needed[i].value || needed[i].value[0] == '\0') {
            continue;
        }

        size_t name_len = strlen(needed[i].name);
        int found = 0;
        for (size_t j = 0; j < count; j++) {
            if (strncmp(envp[j], needed[i].name, name_len) == 0 && envp[j][name_len] == '=') {
                found = 1;
                break;
            }
        }
        if (!found) {
            missing++;
        }
    }

    if (missing == 0) {
        return (char **)envp;
    }

    char **merged = calloc(count + (size_t)missing + 1, sizeof(char *));
    if (!merged) {
        return NULL;
    }

    for (size_t i = 0; i < count; i++) {
        merged[i] = envp[i];
    }

    size_t out = count;
    for (size_t i = 0; i < sizeof(needed) / sizeof(needed[0]); i++) {
        if (!needed[i].value || needed[i].value[0] == '\0') {
            continue;
        }

        size_t name_len = strlen(needed[i].name);
        int found = 0;
        for (size_t j = 0; j < count; j++) {
            if (strncmp(envp[j], needed[i].name, name_len) == 0 && envp[j][name_len] == '=') {
                found = 1;
                break;
            }
        }
        if (found) {
            continue;
        }

        size_t entry_len = name_len + strlen(needed[i].value) + 2;
        merged[out] = malloc(entry_len);
        if (!merged[out]) {
            for (size_t k = count; k < out; k++) {
                free(merged[k]);
            }
            free(merged);
            return NULL;
        }
        snprintf(merged[out], entry_len, "%s=%s", needed[i].name, needed[i].value);
        out++;
    }

    merged[out] = NULL;
    return merged;
}

static void linbox_free_injected_env(char **envp, char *const original[]) {
    if (!envp || envp == original) {
        return;
    }

    size_t original_count = 0;
    if (original) {
        while (original[original_count]) {
            original_count++;
        }
    }

    size_t count = original_count;
    while (envp[count]) {
        free(envp[count]);
        count++;
    }
    free(envp);
}

static int linbox_send_register_process(pid_t pid) {
    linbox_state_t *st = linbox_state();
    if (!st->controller_connected || st->controller_fd < 0) {
        return -1;
    }

    sbp_message_t msg = {
        .type = SBP_MSG_REGISTER_PROCESS,
        .version = SBP_VERSION,
        .flags = 0,
        .payload.register_process = {
            .pid = (uint32_t)pid,
            .slot = 0,
        },
    };
    uint8_t frame[SBP_MAX_FRAME_SIZE];
    size_t out_len = 0;
    if (sbp_serialize_message(&msg, frame, sizeof(frame), &out_len) != SBP_OK) {
        return -1;
    }
    if (write(st->controller_fd, frame, out_len) != (ssize_t)out_len) {
        return -1;
    }

    ssize_t n = read(st->controller_fd, frame, sizeof(frame));
    if (n <= 0) {
        return -1;
    }

    sbp_message_t ack;
    if (sbp_deserialize_message(frame, (size_t)n, &ack) != SBP_OK || ack.type != SBP_MSG_ACK) {
        return -1;
    }

    return 0;
}

static void linbox_after_fork_in_child(void) {
    linbox_reinit_state();
    (void)linbox_send_register_process(getpid());
}

pid_t fork(void) {
    LINBOX_RESOLVE_NEXT(g_real_fork, "fork", real_fork_fn);
    pid_t pid = g_real_fork();
    if (pid == 0) {
        linbox_after_fork_in_child();
    }
    return pid;
}

pid_t vfork(void) { return fork(); }

typedef struct linbox_clone_start {
    int (*fn)(void *);
    void *arg;
    int is_process;
} linbox_clone_start_t;

static int linbox_clone_entry(void *arg) {
    linbox_clone_start_t *start = (linbox_clone_start_t *)arg;
    if (start->is_process) {
        linbox_after_fork_in_child();
    }
    return start->fn(start->arg);
}

int clone(int (*fn)(void *), void *stack, int flags, void *arg, ...) {
    pid_t *ptid = NULL;
    void *tls = NULL;
    pid_t *ctid = NULL;

    va_list ap;
    va_start(ap, arg);
    ptid = va_arg(ap, pid_t *);
    tls = va_arg(ap, void *);
    ctid = va_arg(ap, pid_t *);
    va_end(ap);

    linbox_clone_start_t *start = malloc(sizeof(*start));
    if (!start) {
        errno = ENOMEM;
        return -1;
    }

    start->fn = fn;
    start->arg = arg;
    start->is_process = ((flags & CLONE_VM) == 0);

    LINBOX_RESOLVE_NEXT(g_real_clone, "clone", real_clone_fn);
    int rc = g_real_clone(linbox_clone_entry, stack, flags, start, ptid, tls, ctid);
    if (rc != 0) {
        free(start);
    }
    return rc;
}

#ifdef SYS_clone3
int clone3(struct clone_args *cl_args, size_t size) {
    int rc = (int)linbox_raw_syscall2(SYS_clone3, (long)cl_args, (long)size);
    if (rc == 0 && cl_args && ((cl_args->flags & CLONE_VM) == 0)) {
        linbox_after_fork_in_child();
    }
    return linbox_syscall_result(rc);
}
#endif

int execve(const char *path, char *const argv[], char *const envp[]) {
    LINBOX_RESOLVE_NEXT(g_real_execve, "execve", real_execve_fn);
    char **merged = linbox_inject_runtime_env(envp);
    if (!merged) {
        errno = ENOMEM;
        return -1;
    }
    int rc = g_real_execve(path, argv, merged ? merged : (char *const *)envp);
    linbox_free_injected_env(merged, envp);
    return rc;
}

int execvp(const char *file, char *const argv[]) {
    LINBOX_RESOLVE_NEXT(g_real_execvp, "execvp", real_execvp_fn);
    return g_real_execvp(file, argv);
}

int execvpe(const char *file, char *const argv[], char *const envp[]) {
    LINBOX_RESOLVE_NEXT(g_real_execvpe, "execvpe", real_execvpe_fn);
    char **merged = linbox_inject_runtime_env(envp);
    if (!merged) {
        errno = ENOMEM;
        return -1;
    }
    int rc = g_real_execvpe(file, argv, merged ? merged : (char *const *)envp);
    linbox_free_injected_env(merged, envp);
    return rc;
}

int execv(const char *path, char *const argv[]) { return execve(path, argv, environ); }

int execl(const char *path, const char *arg0, ...) {
    va_list ap;
    size_t argc = 1;

    va_start(ap, arg0);
    while (va_arg(ap, const char *)) {
        argc++;
    }
    va_end(ap);

    char **argv = calloc(argc + 1, sizeof(char *));
    if (!argv) {
        errno = ENOMEM;
        return -1;
    }

    argv[0] = (char *)arg0;
    va_start(ap, arg0);
    for (size_t i = 1; i < argc; i++) {
        argv[i] = (char *)va_arg(ap, const char *);
    }
    va_end(ap);

    int rc = execve(path, argv, environ);
    free(argv);
    return rc;
}

int execle(const char *path, const char *arg0, ...) {
    va_list ap;
    size_t argc = 1;

    va_start(ap, arg0);
    const char *arg = NULL;
    while ((arg = va_arg(ap, const char *))) {
        argc++;
    }
    va_end(ap);

    char **argv = calloc(argc + 1, sizeof(char *));
    if (!argv) {
        errno = ENOMEM;
        return -1;
    }

    argv[0] = (char *)arg0;
    va_start(ap, arg0);
    for (size_t i = 1; i < argc; i++) {
        argv[i] = (char *)va_arg(ap, const char *);
    }
    (void)va_arg(ap, const char *);
    char *const *envp = va_arg(ap, char *const *);
    va_end(ap);

    int rc = execve(path, argv, (char *const *)envp);
    free(argv);
    return rc;
}

int execlp(const char *file, const char *arg0, ...) {
    va_list ap;
    size_t argc = 1;

    va_start(ap, arg0);
    while (va_arg(ap, const char *)) {
        argc++;
    }
    va_end(ap);

    char **argv = calloc(argc + 1, sizeof(char *));
    if (!argv) {
        errno = ENOMEM;
        return -1;
    }

    argv[0] = (char *)arg0;
    va_start(ap, arg0);
    for (size_t i = 1; i < argc; i++) {
        argv[i] = (char *)va_arg(ap, const char *);
    }
    va_end(ap);

    int rc = execvp(file, argv);
    free(argv);
    return rc;
}

int posix_spawn(pid_t *pid, const char *path, const posix_spawn_file_actions_t *file_actions,
                const posix_spawnattr_t *attrp, char *const argv[], char *const envp[]) {
    LINBOX_RESOLVE_NEXT(g_real_posix_spawn, "posix_spawn", real_posix_spawn_fn);
    char **merged = linbox_inject_runtime_env(envp);
    if (!merged) {
        return ENOMEM;
    }
    int rc = g_real_posix_spawn(pid, path, file_actions, attrp, argv,
                                merged ? merged : (char *const *)envp);
    linbox_free_injected_env(merged, envp);
    return rc;
}

int posix_spawnp(pid_t *pid, const char *file, const posix_spawn_file_actions_t *file_actions,
                 const posix_spawnattr_t *attrp, char *const argv[], char *const envp[]) {
    LINBOX_RESOLVE_NEXT(g_real_posix_spawnp, "posix_spawnp", real_posix_spawnp_fn);
    char **merged = linbox_inject_runtime_env(envp);
    if (!merged) {
        return ENOMEM;
    }
    int rc = g_real_posix_spawnp(pid, file, file_actions, attrp, argv,
                                 merged ? merged : (char *const *)envp);
    linbox_free_injected_env(merged, envp);
    return rc;
}
