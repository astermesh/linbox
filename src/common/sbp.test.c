#include <criterion/criterion.h>

#include <string.h>

#include "sbp.h"

Test(sbp, roundtrip_hello) {
    sbp_message_t msg = {.type = SBP_MSG_HELLO, .version = SBP_VERSION, .flags = 0};
    uint8_t buf[SBP_MAX_FRAME_SIZE];
    size_t n = 0;

    cr_assert_eq(sbp_serialize_message(&msg, buf, sizeof(buf), &n), SBP_OK);

    sbp_message_t out;
    cr_assert_eq(sbp_deserialize_message(buf, n, &out), SBP_OK);
    cr_assert_eq(out.type, SBP_MSG_HELLO);
}

Test(sbp, roundtrip_ack) {
    sbp_message_t msg = {.type = SBP_MSG_ACK, .version = SBP_VERSION, .flags = 7};
    uint8_t buf[SBP_MAX_FRAME_SIZE];
    size_t n = 0;

    cr_assert_eq(sbp_serialize_message(&msg, buf, sizeof(buf), &n), SBP_OK);

    sbp_message_t out;
    cr_assert_eq(sbp_deserialize_message(buf, n, &out), SBP_OK);
    cr_assert_eq(out.type, SBP_MSG_ACK);
    cr_assert_eq(out.flags, 7);
}

Test(sbp, roundtrip_set_time) {
    sbp_message_t msg = {
        .type = SBP_MSG_SET_TIME,
        .version = SBP_VERSION,
        .payload.set_time = {.tv_sec = 1735689600, .tv_nsec = 123456789},
    };
    uint8_t buf[SBP_MAX_FRAME_SIZE];
    size_t n = 0;

    cr_assert_eq(sbp_serialize_message(&msg, buf, sizeof(buf), &n), SBP_OK);

    sbp_message_t out;
    cr_assert_eq(sbp_deserialize_message(buf, n, &out), SBP_OK);
    cr_assert_eq(out.type, SBP_MSG_SET_TIME);
    cr_assert_eq(out.payload.set_time.tv_sec, msg.payload.set_time.tv_sec);
    cr_assert_eq(out.payload.set_time.tv_nsec, msg.payload.set_time.tv_nsec);
}

Test(sbp, roundtrip_set_seed) {
    sbp_message_t msg = {
        .type = SBP_MSG_SET_SEED,
        .version = SBP_VERSION,
        .payload.set_seed = {.seed = 0x12345678ABCDEF01ULL},
    };
    uint8_t buf[SBP_MAX_FRAME_SIZE];
    size_t n = 0;

    cr_assert_eq(sbp_serialize_message(&msg, buf, sizeof(buf), &n), SBP_OK);

    sbp_message_t out;
    cr_assert_eq(sbp_deserialize_message(buf, n, &out), SBP_OK);
    cr_assert_eq(out.type, SBP_MSG_SET_SEED);
    cr_assert_eq(out.payload.set_seed.seed, msg.payload.set_seed.seed);
}

Test(sbp, roundtrip_register_process) {
    sbp_message_t msg = {
        .type = SBP_MSG_REGISTER_PROCESS,
        .version = SBP_VERSION,
        .payload.register_process = {.pid = 4242, .slot = 5},
    };
    uint8_t buf[SBP_MAX_FRAME_SIZE];
    size_t n = 0;

    cr_assert_eq(sbp_serialize_message(&msg, buf, sizeof(buf), &n), SBP_OK);

    sbp_message_t out;
    cr_assert_eq(sbp_deserialize_message(buf, n, &out), SBP_OK);
    cr_assert_eq(out.type, SBP_MSG_REGISTER_PROCESS);
    cr_assert_eq(out.payload.register_process.pid, 4242);
    cr_assert_eq(out.payload.register_process.slot, 5);
}

Test(sbp, boundary_zero_length_payload) {
    uint8_t frame[SBP_HEADER_SIZE];
    size_t out_len = 0;
    cr_assert_eq(
        sbp_encode_frame(SBP_VERSION, SBP_MSG_HELLO, 0, NULL, 0, frame, sizeof(frame), &out_len),
        SBP_OK);
    cr_assert_eq(out_len, SBP_HEADER_SIZE);

    sbp_header_t h;
    const uint8_t *payload = NULL;
    cr_assert_eq(sbp_decode_frame(frame, out_len, &h, &payload), SBP_OK);
    cr_assert_eq(h.payload_len, 0);
}

Test(sbp, boundary_max_payload_frame) {
    uint8_t payload[SBP_MAX_PAYLOAD];
    memset(payload, 0xA5, sizeof(payload));

    static uint8_t frame[SBP_MAX_FRAME_SIZE];
    size_t out_len = 0;

    cr_assert_eq(sbp_encode_frame(SBP_VERSION, 200, 0x55AA, payload, SBP_MAX_PAYLOAD, frame,
                                  sizeof(frame), &out_len),
                 SBP_OK);
    cr_assert_eq(out_len, SBP_MAX_FRAME_SIZE);

    sbp_header_t h;
    const uint8_t *out_payload = NULL;
    cr_assert_eq(sbp_decode_frame(frame, out_len, &h, &out_payload), SBP_OK);
    cr_assert_eq(h.payload_len, SBP_MAX_PAYLOAD);
    cr_assert_eq(memcmp(payload, out_payload, SBP_MAX_PAYLOAD), 0);
}

Test(sbp, invalid_message_type_graceful_error) {
    uint8_t frame[SBP_HEADER_SIZE] = {SBP_VERSION, 0xFF, 0, 0, 0, 0, 0, 0};
    sbp_message_t msg;
    cr_assert_eq(sbp_deserialize_message(frame, sizeof(frame), &msg), SBP_ERR_INVALID_TYPE);
}
