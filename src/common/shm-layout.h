#ifndef LINBOX_COMMON_SHM_LAYOUT_H
#define LINBOX_COMMON_SHM_LAYOUT_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define LINBOX_SHM_MAGIC 0x4C494E42u /* "LINB" */
#define LINBOX_SHM_VERSION 1u
#define LINBOX_PROCESS_SLOT_COUNT 64u
#define LINBOX_POLICY_AREA_SIZE 4096u
#define LINBOX_LAYOUT_RESERVED_SIZE 1024u

#define LINBOX_FLAG_PAUSED (1u << 0)
#define LINBOX_FLAG_STEPPING (1u << 1)

typedef struct linbox_process_slot {
    uint32_t pid;
    uint32_t flags;
    uint64_t heartbeat_ns;
    uint8_t reserved[16];
} linbox_process_slot_t;

typedef struct linbox_shm_header {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    _Atomic uint32_t global_seq;
    _Atomic uint64_t heartbeat_ns;
    uint32_t network_policy_offset;
    uint32_t filesystem_policy_offset;
    uint32_t dns_policy_offset;
    uint32_t policy_area_size;
    uint8_t reserved[32];
} linbox_shm_header_t;

typedef struct linbox_shm_layout {
    linbox_shm_header_t header;

    _Atomic uint32_t time_seq;
    struct timespec virtual_time;

    _Atomic uint64_t prng_seed;
    _Atomic uint32_t flags;

    linbox_process_slot_t process_slots[LINBOX_PROCESS_SLOT_COUNT];

    uint8_t policy_area[LINBOX_POLICY_AREA_SIZE];
    uint8_t reserved[LINBOX_LAYOUT_RESERVED_SIZE];
} linbox_shm_layout_t;

typedef struct linbox_shm_handle {
    int fd;
    size_t size;
    linbox_shm_layout_t *layout;
} linbox_shm_handle_t;

void linbox_shm_init_layout(linbox_shm_layout_t *layout);

int linbox_shm_create(const char *path, linbox_shm_handle_t *out);
int linbox_shm_attach(const char *path, linbox_shm_handle_t *out);
void linbox_shm_detach(linbox_shm_handle_t *handle);
int linbox_shm_destroy(const char *path, linbox_shm_handle_t *handle);

void linbox_shm_write_time(linbox_shm_layout_t *layout, const struct timespec *ts);
int linbox_shm_read_time(const linbox_shm_layout_t *layout, struct timespec *out);

#endif