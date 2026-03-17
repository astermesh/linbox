#include "sbp.h"

#include <string.h>

static uint16_t read_le16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }

static uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t read_le64(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) {
        v |= ((uint64_t)p[i] << (8 * i));
    }
    return v;
}

static void write_le16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void write_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static void write_le64(uint8_t *p, uint64_t v) {
    for (int i = 0; i < 8; i++) {
        p[i] = (uint8_t)((v >> (8 * i)) & 0xFFu);
    }
}

size_t sbp_payload_len_for_type(sbp_message_type_t type) {
    switch (type) {
    case SBP_MSG_HELLO:
    case SBP_MSG_ACK:
        return 0;
    case SBP_MSG_SET_TIME:
        return sizeof(int64_t) + sizeof(int32_t);
    case SBP_MSG_SET_SEED:
        return sizeof(uint64_t);
    case SBP_MSG_REGISTER_PROCESS:
        return sizeof(uint32_t) + sizeof(uint32_t);
    default:
        return 0;
    }
}

const char *sbp_status_str(int status) {
    switch (status) {
    case SBP_OK:
        return "ok";
    case SBP_ERR_INVALID_ARG:
        return "invalid-arg";
    case SBP_ERR_BUFFER_TOO_SMALL:
        return "buffer-too-small";
    case SBP_ERR_INVALID_TYPE:
        return "invalid-type";
    case SBP_ERR_INVALID_LENGTH:
        return "invalid-length";
    case SBP_ERR_UNSUPPORTED_VERSION:
        return "unsupported-version";
    default:
        return "unknown";
    }
}

int sbp_encode_frame(uint8_t version, uint8_t type, uint16_t flags, const uint8_t *payload,
                     uint32_t payload_len, uint8_t *out, size_t out_cap, size_t *out_len) {
    if (!out || !out_len) {
        return SBP_ERR_INVALID_ARG;
    }

    if (payload_len > SBP_MAX_PAYLOAD) {
        return SBP_ERR_INVALID_LENGTH;
    }

    size_t total = SBP_HEADER_SIZE + payload_len;
    if (out_cap < total) {
        return SBP_ERR_BUFFER_TOO_SMALL;
    }

    out[0] = version;
    out[1] = type;
    write_le16(out + 2, flags);
    write_le32(out + 4, payload_len);

    if (payload_len > 0 && payload) {
        memcpy(out + SBP_HEADER_SIZE, payload, payload_len);
    }

    *out_len = total;
    return SBP_OK;
}

int sbp_decode_frame(const uint8_t *frame, size_t frame_len, sbp_header_t *out_header,
                     const uint8_t **out_payload) {
    if (!frame || !out_header) {
        return SBP_ERR_INVALID_ARG;
    }

    if (frame_len < SBP_HEADER_SIZE) {
        return SBP_ERR_INVALID_LENGTH;
    }

    sbp_header_t h;
    h.version = frame[0];
    h.type = frame[1];
    h.flags = read_le16(frame + 2);
    h.payload_len = read_le32(frame + 4);

    if (h.payload_len > SBP_MAX_PAYLOAD) {
        return SBP_ERR_INVALID_LENGTH;
    }

    if (frame_len != SBP_HEADER_SIZE + h.payload_len) {
        return SBP_ERR_INVALID_LENGTH;
    }

    *out_header = h;
    if (out_payload) {
        *out_payload = frame + SBP_HEADER_SIZE;
    }

    return SBP_OK;
}

int sbp_serialize_message(const sbp_message_t *msg, uint8_t *out, size_t out_cap, size_t *out_len) {
    if (!msg) {
        return SBP_ERR_INVALID_ARG;
    }

    uint8_t payload[16] = {0};
    uint32_t payload_len = (uint32_t)sbp_payload_len_for_type(msg->type);

    switch (msg->type) {
    case SBP_MSG_HELLO:
    case SBP_MSG_ACK:
        payload_len = 0;
        break;
    case SBP_MSG_SET_TIME:
        write_le64(payload, (uint64_t)msg->payload.set_time.tv_sec);
        write_le32(payload + 8, (uint32_t)msg->payload.set_time.tv_nsec);
        break;
    case SBP_MSG_SET_SEED:
        write_le64(payload, msg->payload.set_seed.seed);
        break;
    case SBP_MSG_REGISTER_PROCESS:
        write_le32(payload, msg->payload.register_process.pid);
        write_le32(payload + 4, msg->payload.register_process.slot);
        break;
    default:
        return SBP_ERR_INVALID_TYPE;
    }

    return sbp_encode_frame(msg->version, (uint8_t)msg->type, msg->flags,
                            payload_len ? payload : NULL, payload_len, out, out_cap, out_len);
}

int sbp_deserialize_message(const uint8_t *frame, size_t frame_len, sbp_message_t *out_msg) {
    if (!out_msg) {
        return SBP_ERR_INVALID_ARG;
    }

    sbp_header_t h;
    const uint8_t *payload = NULL;
    int rc = sbp_decode_frame(frame, frame_len, &h, &payload);
    if (rc != SBP_OK) {
        return rc;
    }

    if (h.version != SBP_VERSION) {
        return SBP_ERR_UNSUPPORTED_VERSION;
    }

    memset(out_msg, 0, sizeof(*out_msg));
    out_msg->version = h.version;
    out_msg->flags = h.flags;
    out_msg->type = (sbp_message_type_t)h.type;

    switch (out_msg->type) {
    case SBP_MSG_HELLO:
    case SBP_MSG_ACK:
        if (h.payload_len != 0) {
            return SBP_ERR_INVALID_LENGTH;
        }
        break;
    case SBP_MSG_SET_TIME:
        if (h.payload_len != sbp_payload_len_for_type(SBP_MSG_SET_TIME)) {
            return SBP_ERR_INVALID_LENGTH;
        }
        out_msg->payload.set_time.tv_sec = (int64_t)read_le64(payload);
        out_msg->payload.set_time.tv_nsec = (int32_t)read_le32(payload + 8);
        break;
    case SBP_MSG_SET_SEED:
        if (h.payload_len != sbp_payload_len_for_type(SBP_MSG_SET_SEED)) {
            return SBP_ERR_INVALID_LENGTH;
        }
        out_msg->payload.set_seed.seed = read_le64(payload);
        break;
    case SBP_MSG_REGISTER_PROCESS:
        if (h.payload_len != sbp_payload_len_for_type(SBP_MSG_REGISTER_PROCESS)) {
            return SBP_ERR_INVALID_LENGTH;
        }
        out_msg->payload.register_process.pid = read_le32(payload);
        out_msg->payload.register_process.slot = read_le32(payload + 4);
        break;
    default:
        return SBP_ERR_INVALID_TYPE;
    }

    return SBP_OK;
}
