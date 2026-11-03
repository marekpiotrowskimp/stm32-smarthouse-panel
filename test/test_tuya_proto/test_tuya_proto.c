/*
 * Kontrakt biblioteki tuya_proto (spec: tuya-proto, docs/SPEC.md §5).
 * Oczekiwane bajty pochodzą z referencyjnej biblioteki tinytuya (vectors.h,
 * generowane przez tools/gen_tuya_vectors.py) – ta sama implementacja sterowała
 * żarówką „Sypialnia” w sieci domowej.
 */
#include <string.h>
#include <unity.h>

#include "tuya_proto.h"
#include "vectors.h"

#define KEY ((const uint8_t *)VEC_KEY)

static uint8_t buf[512];
static char json[512];

void setUp(void)
{
    memset(buf, 0xEE, sizeof buf);
    memset(json, 0x7F, sizeof json);
}

void tearDown(void) {}

/* ---------- AC-1: CRC32 ---------- */

void test_crc32_of_check_string_is_cbf43926(void)
{
    TEST_ASSERT_EQUAL_HEX32(VEC_CRC_EXPECTED,
                            tuya_crc32((const uint8_t *)VEC_CRC_INPUT, strlen(VEC_CRC_INPUT)));
}

void test_crc32_of_empty_input_is_zero(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, tuya_crc32((const uint8_t *)"", 0));
}

/* ---------- AC-2: AES-128-ECB + PKCS#7 – szyfrowanie ---------- */

void test_aes_encrypt_17_bytes_gives_exact_32_byte_ciphertext(void)
{
    int n = tuya_aes_encrypt(KEY, (const uint8_t *)VEC_AES_PLAIN, strlen(VEC_AES_PLAIN), buf, sizeof buf);
    TEST_ASSERT_EQUAL_INT(VEC_AES_CIPHER_LEN, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(vec_aes_cipher, buf, VEC_AES_CIPHER_LEN);
}

void test_aes_encrypt_16_bytes_adds_full_padding_block(void)
{
    int n = tuya_aes_encrypt(KEY, (const uint8_t *)VEC_AES_PLAIN16, 16, buf, sizeof buf);
    TEST_ASSERT_EQUAL_INT(32, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(vec_aes_cipher16, buf, VEC_AES_CIPHER16_LEN);
}

void test_aes_encrypt_into_too_small_buffer_returns_buf_small(void)
{
    int n = tuya_aes_encrypt(KEY, (const uint8_t *)VEC_AES_PLAIN, strlen(VEC_AES_PLAIN), buf, 31);
    TEST_ASSERT_EQUAL_INT(TUYA_ERR_BUF_SMALL, n);
}

/* ---------- AC-3: AES – deszyfrowanie ---------- */

void test_aes_decrypt_returns_original_plaintext_without_padding(void)
{
    int n = tuya_aes_decrypt(KEY, vec_aes_cipher, VEC_AES_CIPHER_LEN, buf, sizeof buf);
    TEST_ASSERT_EQUAL_INT(strlen(VEC_AES_PLAIN), n);
    TEST_ASSERT_EQUAL_MEMORY(VEC_AES_PLAIN, buf, strlen(VEC_AES_PLAIN));
}

void test_aes_decrypt_with_wrong_key_returns_decrypt_error(void)
{
    int n = tuya_aes_decrypt((const uint8_t *)"fedcba9876543210", vec_aes_cipher, VEC_AES_CIPHER_LEN,
                             buf, sizeof buf);
    TEST_ASSERT_EQUAL_INT(TUYA_ERR_DECRYPT, n);
}

void test_aes_decrypt_length_not_multiple_of_16_returns_decrypt_error(void)
{
    int n = tuya_aes_decrypt(KEY, vec_aes_cipher, 31, buf, sizeof buf);
    TEST_ASSERT_EQUAL_INT(TUYA_ERR_DECRYPT, n);
}

/* ---------- AC-4..6: budowanie ramek ---------- */

void test_build_control_frame_matches_reference_bytes(void)
{
    int n = tuya_build_frame(1, TUYA_CMD_CONTROL, VEC_CONTROL_JSON, KEY, buf, sizeof buf);
    TEST_ASSERT_EQUAL_INT(VEC_CONTROL_FRAME_LEN, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(vec_control_frame, buf, VEC_CONTROL_FRAME_LEN);
}

void test_build_control_frame_payload_starts_with_version_header(void)
{
    static const uint8_t hdr[TUYA_VER33_HDR_LEN] = { '3', '.', '3', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    int n = tuya_build_frame(1, TUYA_CMD_CONTROL, VEC_CONTROL_JSON, KEY, buf, sizeof buf);
    TEST_ASSERT_EQUAL_INT(VEC_CONTROL_FRAME_LEN, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(hdr, buf + TUYA_HEADER_LEN, TUYA_VER33_HDR_LEN);
}

void test_build_dp_query_frame_matches_reference_bytes_without_version_header(void)
{
    int n = tuya_build_frame(2, TUYA_CMD_DP_QUERY, VEC_QUERY_JSON, KEY, buf, sizeof buf);
    TEST_ASSERT_EQUAL_INT(VEC_QUERY_FRAME_LEN, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(vec_query_frame, buf, VEC_QUERY_FRAME_LEN);
}

void test_build_heartbeat_frame_is_24_reference_bytes(void)
{
    int n = tuya_build_frame(3, TUYA_CMD_HEART_BEAT, NULL, KEY, buf, sizeof buf);
    TEST_ASSERT_EQUAL_INT(24, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(vec_heartbeat_frame, buf, VEC_HEARTBEAT_FRAME_LEN);
}

void test_build_frame_into_too_small_buffer_returns_buf_small(void)
{
    int n = tuya_build_frame(1, TUYA_CMD_CONTROL, VEC_CONTROL_JSON, KEY, buf, VEC_CONTROL_FRAME_LEN - 1);
    TEST_ASSERT_EQUAL_INT(TUYA_ERR_BUF_SMALL, n);
}

/* ---------- AC-7: parsowanie poprawnych ramek ---------- */

void test_parse_device_response_reads_header_retcode_and_payload(void)
{
    tuya_frame_t f = {0};
    TEST_ASSERT_EQUAL_INT(TUYA_OK, tuya_parse_frame(vec_resp_frame, VEC_RESP_FRAME_LEN, &f));
    TEST_ASSERT_EQUAL_UINT32(2, f.seq);
    TEST_ASSERT_EQUAL_UINT32(TUYA_CMD_DP_QUERY, f.cmd);
    TEST_ASSERT_TRUE(f.has_retcode);
    TEST_ASSERT_EQUAL_UINT32(0, f.retcode);
    TEST_ASSERT_EQUAL_UINT(VEC_RESP_FRAME_LEN - TUYA_HEADER_LEN - 4 - TUYA_TRAILER_LEN, f.payload_len);
    TEST_ASSERT_EQUAL_PTR(vec_resp_frame + TUYA_HEADER_LEN + 4, f.payload);
    TEST_ASSERT_EQUAL_UINT(VEC_RESP_FRAME_LEN, f.frame_len);
}

void test_parse_own_control_frame_has_no_retcode(void)
{
    tuya_frame_t f = {0};
    TEST_ASSERT_EQUAL_INT(TUYA_OK, tuya_parse_frame(vec_control_frame, VEC_CONTROL_FRAME_LEN, &f));
    TEST_ASSERT_EQUAL_UINT32(TUYA_CMD_CONTROL, f.cmd);
    TEST_ASSERT_FALSE(f.has_retcode);
    TEST_ASSERT_EQUAL_UINT(VEC_CONTROL_FRAME_LEN - TUYA_HEADER_LEN - TUYA_TRAILER_LEN, f.payload_len);
}

void test_parse_heartbeat_has_empty_payload(void)
{
    tuya_frame_t f = {0};
    TEST_ASSERT_EQUAL_INT(TUYA_OK, tuya_parse_frame(vec_heartbeat_frame, VEC_HEARTBEAT_FRAME_LEN, &f));
    TEST_ASSERT_EQUAL_UINT32(3, f.seq);
    TEST_ASSERT_EQUAL_UINT32(TUYA_CMD_HEART_BEAT, f.cmd);
    TEST_ASSERT_EQUAL_UINT(0, f.payload_len);
    TEST_ASSERT_EQUAL_UINT(24, f.frame_len);
}

void test_parse_two_concatenated_frames_one_by_one(void)
{
    tuya_frame_t f = {0};
    memcpy(buf, vec_resp_frame, VEC_RESP_FRAME_LEN);
    memcpy(buf + VEC_RESP_FRAME_LEN, vec_heartbeat_frame, VEC_HEARTBEAT_FRAME_LEN);
    size_t total = VEC_RESP_FRAME_LEN + VEC_HEARTBEAT_FRAME_LEN;

    TEST_ASSERT_EQUAL_INT(TUYA_OK, tuya_parse_frame(buf, total, &f));
    TEST_ASSERT_EQUAL_UINT32(TUYA_CMD_DP_QUERY, f.cmd);
    TEST_ASSERT_EQUAL_UINT(VEC_RESP_FRAME_LEN, f.frame_len);

    TEST_ASSERT_EQUAL_INT(TUYA_OK, tuya_parse_frame(buf + f.frame_len, total - f.frame_len, &f));
    TEST_ASSERT_EQUAL_UINT32(TUYA_CMD_HEART_BEAT, f.cmd);
    TEST_ASSERT_EQUAL_UINT(VEC_HEARTBEAT_FRAME_LEN, f.frame_len);
}

/* ---------- AC-8: niekompletny strumień ---------- */

void test_parse_shorter_than_header_returns_incomplete(void)
{
    tuya_frame_t f = {0};
    TEST_ASSERT_EQUAL_INT(TUYA_ERR_INCOMPLETE, tuya_parse_frame(vec_resp_frame, 10, &f));
}

void test_parse_frame_missing_last_byte_returns_incomplete(void)
{
    tuya_frame_t f = {0};
    TEST_ASSERT_EQUAL_INT(TUYA_ERR_INCOMPLETE, tuya_parse_frame(vec_resp_frame, VEC_RESP_FRAME_LEN - 1, &f));
}

/* ---------- AC-9: uszkodzone ramki (R-TUYA-2) ---------- */

void test_parse_corrupted_crc_returns_crc_error_and_frame_len_to_skip(void)
{
    tuya_frame_t f = {0};
    memcpy(buf, vec_resp_frame, VEC_RESP_FRAME_LEN);
    buf[VEC_RESP_FRAME_LEN - 5] ^= 0x01; /* ostatni bajt CRC */
    TEST_ASSERT_EQUAL_INT(TUYA_ERR_CRC, tuya_parse_frame(buf, VEC_RESP_FRAME_LEN, &f));
    TEST_ASSERT_EQUAL_UINT(VEC_RESP_FRAME_LEN, f.frame_len);
}

void test_parse_corrupted_payload_byte_returns_crc_error(void)
{
    tuya_frame_t f = {0};
    memcpy(buf, vec_resp_frame, VEC_RESP_FRAME_LEN);
    buf[30] ^= 0x80;
    TEST_ASSERT_EQUAL_INT(TUYA_ERR_CRC, tuya_parse_frame(buf, VEC_RESP_FRAME_LEN, &f));
}

void test_parse_bad_prefix_returns_prefix_error(void)
{
    tuya_frame_t f = {0};
    memcpy(buf, vec_resp_frame, VEC_RESP_FRAME_LEN);
    buf[2] = 0x66; /* 00006699 – protokół 3.5, nieobsługiwany */
    buf[3] = 0x99;
    TEST_ASSERT_EQUAL_INT(TUYA_ERR_PREFIX, tuya_parse_frame(buf, VEC_RESP_FRAME_LEN, &f));
}

void test_parse_bad_suffix_returns_suffix_error_and_frame_len(void)
{
    tuya_frame_t f = {0};
    memcpy(buf, vec_resp_frame, VEC_RESP_FRAME_LEN);
    buf[VEC_RESP_FRAME_LEN - 1] = 0x00;
    TEST_ASSERT_EQUAL_INT(TUYA_ERR_SUFFIX, tuya_parse_frame(buf, VEC_RESP_FRAME_LEN, &f));
    TEST_ASSERT_EQUAL_UINT(VEC_RESP_FRAME_LEN, f.frame_len);
}

void test_parse_len_field_above_max_returns_len_error(void)
{
    tuya_frame_t f = {0};
    memcpy(buf, vec_resp_frame, VEC_RESP_FRAME_LEN);
    buf[12] = 0x00; buf[13] = 0x01; buf[14] = 0x00; buf[15] = 0x00; /* len = 65536 */
    TEST_ASSERT_EQUAL_INT(TUYA_ERR_LEN, tuya_parse_frame(buf, VEC_RESP_FRAME_LEN, &f));
}

void test_parse_len_field_below_trailer_returns_len_error(void)
{
    tuya_frame_t f = {0};
    memcpy(buf, vec_heartbeat_frame, VEC_HEARTBEAT_FRAME_LEN);
    buf[15] = 0x04; /* len = 4 < crc + suffix */
    TEST_ASSERT_EQUAL_INT(TUYA_ERR_LEN, tuya_parse_frame(buf, VEC_HEARTBEAT_FRAME_LEN, &f));
}

/* ---------- AC-10: deszyfrowanie payloadu odebranej ramki ---------- */

void test_decode_dp_query_response_gives_exact_json(void)
{
    tuya_frame_t f = {0};
    TEST_ASSERT_EQUAL_INT(TUYA_OK, tuya_parse_frame(vec_resp_frame, VEC_RESP_FRAME_LEN, &f));
    int n = tuya_decode_payload(&f, KEY, json, sizeof json);
    TEST_ASSERT_EQUAL_INT(strlen(VEC_RESP_JSON), n);
    TEST_ASSERT_EQUAL_STRING(VEC_RESP_JSON, json);
}

void test_decode_status_push_skips_version_header(void)
{
    tuya_frame_t f = {0};
    TEST_ASSERT_EQUAL_INT(TUYA_OK, tuya_parse_frame(vec_push_frame, VEC_PUSH_FRAME_LEN, &f));
    TEST_ASSERT_EQUAL_UINT32(TUYA_CMD_STATUS, f.cmd);
    int n = tuya_decode_payload(&f, KEY, json, sizeof json);
    TEST_ASSERT_EQUAL_INT(strlen(VEC_PUSH_JSON), n);
    TEST_ASSERT_EQUAL_STRING(VEC_PUSH_JSON, json);
}

/* Prawdziwe pushe po komendzie (np. żarówka, rolety): po "3.3" niezerowe bajty (seq 0x57, 0x01) */
void test_decode_status_push_with_nonzero_version_header_bytes_gives_exact_json(void)
{
    static const uint8_t hdr[TUYA_VER33_HDR_LEN] = { '3', '.', '3', 0, 0, 0, 0, 0, 0, 0, 0x57, 0, 0, 0, 0x01 };
    static uint8_t payload[TUYA_VER33_HDR_LEN + 128];
    memcpy(payload, hdr, sizeof hdr);
    int c = tuya_aes_encrypt(KEY, (const uint8_t *)VEC_PUSH_JSON, strlen(VEC_PUSH_JSON), payload + sizeof hdr,
                             sizeof payload - sizeof hdr);
    TEST_ASSERT_GREATER_THAN_INT(0, c);
    tuya_frame_t f = {0};
    f.cmd = TUYA_CMD_STATUS;
    f.payload = payload;
    f.payload_len = sizeof hdr + (size_t)c;
    int n = tuya_decode_payload(&f, KEY, json, sizeof json);
    TEST_ASSERT_EQUAL_INT(strlen(VEC_PUSH_JSON), n);
    TEST_ASSERT_EQUAL_STRING(VEC_PUSH_JSON, json);
}

void test_decode_heartbeat_gives_empty_string(void)
{
    tuya_frame_t f = {0};
    TEST_ASSERT_EQUAL_INT(TUYA_OK, tuya_parse_frame(vec_heartbeat_frame, VEC_HEARTBEAT_FRAME_LEN, &f));
    TEST_ASSERT_EQUAL_INT(0, tuya_decode_payload(&f, KEY, json, sizeof json));
    TEST_ASSERT_EQUAL_STRING("", json);
}

void test_decode_with_wrong_key_returns_decrypt_error(void)
{
    tuya_frame_t f = {0};
    TEST_ASSERT_EQUAL_INT(TUYA_OK, tuya_parse_frame(vec_resp_frame, VEC_RESP_FRAME_LEN, &f));
    TEST_ASSERT_EQUAL_INT(TUYA_ERR_DECRYPT,
                          tuya_decode_payload(&f, (const uint8_t *)"fedcba9876543210", json, sizeof json));
}

void test_decode_into_buffer_without_room_for_nul_returns_buf_small(void)
{
    tuya_frame_t f = {0};
    TEST_ASSERT_EQUAL_INT(TUYA_OK, tuya_parse_frame(vec_resp_frame, VEC_RESP_FRAME_LEN, &f));
    TEST_ASSERT_EQUAL_INT(TUYA_ERR_BUF_SMALL, tuya_decode_payload(&f, KEY, json, strlen(VEC_RESP_JSON)));
}

/* ---------- AC-11: broadcast discovery UDP 6667 (R-TUYA-6) ---------- */

void test_decode_discovery_broadcast_gives_exact_json(void)
{
    int n = tuya_decode_discovery(vec_disc_frame, VEC_DISC_FRAME_LEN, json, sizeof json);
    TEST_ASSERT_EQUAL_INT(strlen(VEC_DISC_JSON), n);
    TEST_ASSERT_EQUAL_STRING(VEC_DISC_JSON, json);
}

void test_decode_discovery_with_corrupted_crc_returns_crc_error(void)
{
    memcpy(buf, vec_disc_frame, VEC_DISC_FRAME_LEN);
    buf[40] ^= 0x01;
    TEST_ASSERT_EQUAL_INT(TUYA_ERR_CRC, tuya_decode_discovery(buf, VEC_DISC_FRAME_LEN, json, sizeof json));
}

/* ---------- AC-12: pełny obieg (round-trip) ---------- */

void test_built_control_frame_parses_and_decodes_back_to_same_json(void)
{
    tuya_frame_t f = {0};
    int n = tuya_build_frame(42, TUYA_CMD_CONTROL, VEC_CONTROL_JSON, KEY, buf, sizeof buf);
    TEST_ASSERT_EQUAL_INT(VEC_CONTROL_FRAME_LEN, n);
    TEST_ASSERT_EQUAL_INT(TUYA_OK, tuya_parse_frame(buf, (size_t)n, &f));
    TEST_ASSERT_EQUAL_UINT32(42, f.seq);
    TEST_ASSERT_EQUAL_INT(strlen(VEC_CONTROL_JSON), tuya_decode_payload(&f, KEY, json, sizeof json));
    TEST_ASSERT_EQUAL_STRING(VEC_CONTROL_JSON, json);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_crc32_of_check_string_is_cbf43926);
    RUN_TEST(test_crc32_of_empty_input_is_zero);
    RUN_TEST(test_aes_encrypt_17_bytes_gives_exact_32_byte_ciphertext);
    RUN_TEST(test_aes_encrypt_16_bytes_adds_full_padding_block);
    RUN_TEST(test_aes_encrypt_into_too_small_buffer_returns_buf_small);
    RUN_TEST(test_aes_decrypt_returns_original_plaintext_without_padding);
    RUN_TEST(test_aes_decrypt_with_wrong_key_returns_decrypt_error);
    RUN_TEST(test_aes_decrypt_length_not_multiple_of_16_returns_decrypt_error);
    RUN_TEST(test_build_control_frame_matches_reference_bytes);
    RUN_TEST(test_build_control_frame_payload_starts_with_version_header);
    RUN_TEST(test_build_dp_query_frame_matches_reference_bytes_without_version_header);
    RUN_TEST(test_build_heartbeat_frame_is_24_reference_bytes);
    RUN_TEST(test_build_frame_into_too_small_buffer_returns_buf_small);
    RUN_TEST(test_parse_device_response_reads_header_retcode_and_payload);
    RUN_TEST(test_parse_own_control_frame_has_no_retcode);
    RUN_TEST(test_parse_heartbeat_has_empty_payload);
    RUN_TEST(test_parse_two_concatenated_frames_one_by_one);
    RUN_TEST(test_parse_shorter_than_header_returns_incomplete);
    RUN_TEST(test_parse_frame_missing_last_byte_returns_incomplete);
    RUN_TEST(test_parse_corrupted_crc_returns_crc_error_and_frame_len_to_skip);
    RUN_TEST(test_parse_corrupted_payload_byte_returns_crc_error);
    RUN_TEST(test_parse_bad_prefix_returns_prefix_error);
    RUN_TEST(test_parse_bad_suffix_returns_suffix_error_and_frame_len);
    RUN_TEST(test_parse_len_field_above_max_returns_len_error);
    RUN_TEST(test_parse_len_field_below_trailer_returns_len_error);
    RUN_TEST(test_decode_dp_query_response_gives_exact_json);
    RUN_TEST(test_decode_status_push_skips_version_header);
    RUN_TEST(test_decode_status_push_with_nonzero_version_header_bytes_gives_exact_json);
    RUN_TEST(test_decode_heartbeat_gives_empty_string);
    RUN_TEST(test_decode_with_wrong_key_returns_decrypt_error);
    RUN_TEST(test_decode_into_buffer_without_room_for_nul_returns_buf_small);
    RUN_TEST(test_decode_discovery_broadcast_gives_exact_json);
    RUN_TEST(test_decode_discovery_with_corrupted_crc_returns_crc_error);
    RUN_TEST(test_built_control_frame_parses_and_decodes_back_to_same_json);
    return UNITY_END();
}
