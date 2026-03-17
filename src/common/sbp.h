#ifndef LINBOX_COMMON_SBP_H
#define LINBOX_COMMON_SBP_H

#include <stddef.h>
#include <stdint.h>

#define SBP_VERSION 1u
#define SBP_HEADER_SIZE 8u
#define SBP_MAX_PAYLOAD 65535u
#define SBP_MAX_FRAME_SIZE (SBP_HEADER_SIZE + SBP_MAX_PAYLOAD)

typedef enum sbp_message_type {
    SBP_MSG_HELLO = 1,
    SBP_MSG_ACK = 2,
    SBP_MSG_SET_TIME = 3,
    SBP_MSG_SET_SEED = 4,
    SBP_MSG_REGISTER_PROCESS = 5,
} sbp_message_type_t;

typedef enum sbp_status {
    SBP_OK = 0,
    SBP_ERR_INVALID_ARG = -1,
    SBP_ERR_BUFFER_TOO_SMALL = -2,
    SBP_ERR_INVALID_TYPE = -3,
    SBP_ERR_INVALID_LENGTH = -4,
    SBP_ERR_UNSUPPORTED_VERSION = -5,
} sbp_status_t;

typedef struct sbp_header {
    uint8_t version;
    uint8_t type;
    uint16_t flags;
    uint32_t payload_len;
} sbp_header_t;

typedef struct sbp_set_time_payload {
    int64_t tv_sec;
    int32_t tv_nsec;
} sbp_set_time_payload_t;

typedef struct sbp_set_seed_payload {
    uint64_t seed;
} sbp_set_seed_payload_t;

typedef struct sbp_register_process_payload {
    uint32_t pid;
    uint32_t slot;
} sbp_register_process_payload_t;

typedef struct sbp_message {
    sbp_message_type_t type;
    uint8_t version;
    uint16_t flags;
    union {
        sbp_set_time_payload_t set_time;
        sbp_set_seed_payload_t set_seed;
        sbp_register_process_payload_t register_process;
    } payload;
} sbp_message_t;

size_t sbp_payload_len_for_type(sbp_message_type_t type);
const char *sbp_status_str(int status);

int sbp_encode_frame(uint8_t version, uint8_t type, uint16_t flags, const uint8_t *payload,
                     uint32_t payload_len, uint8_t *out, size_t out_cap, size_t *out_len);

int sbp_decode_frame(const uint8_t *frame, size_t frame_len, sbp_header_t *out_header,
                     const uint8_t **out_payload);

int sbp_serialize_message(const sbp_message_t *msg, uint8_t *out, size_t out_cap, size_t *out_len);

int sbp_deserialize_message(const uint8_t *frame, size_t frame_len, sbp_message_t *out_msg);

#endif