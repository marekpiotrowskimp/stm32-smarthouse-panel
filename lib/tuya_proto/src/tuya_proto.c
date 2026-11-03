/*
 * Protokół lokalny Tuya 3.3 – implementacja (docs/SPEC.md §5).
 * AES-128 zaimplementowany lokalnie, żeby biblioteka działała bez zmian
 * na hoście (testy) i na STM32F746 (brak sprzętowego CRYP w F746).
 */
#include "tuya_proto.h"

#include <string.h>

#define TUYA_PREFIX 0x000055AAu
#define TUYA_SUFFIX 0x0000AA55u
#define AES_BLOCK   16u
#define AES_ROUNDS  10u

static const uint8_t VER33_HDR[TUYA_VER33_HDR_LEN] = { '3', '.', '3' };

/* md5("yGAdlopoPVldABfn") – stały klucz broadcastów UDP 6667 */
static const uint8_t UDP_KEY[TUYA_KEY_LEN] = {
    0x6C, 0x1E, 0xC8, 0xE2, 0xBB, 0x9B, 0xB5, 0x9A, 0xB5, 0x0B, 0x0D, 0xAF, 0x64, 0x9B, 0x41, 0x0A
};

/* ------------------------------------------------------------------ */
/* Pomocnicze big-endian                                               */
/* ------------------------------------------------------------------ */

static uint32_t get_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/* ------------------------------------------------------------------ */
/* CRC-32 IEEE 802.3 (odwrócony wielomian 0xEDB88320)                  */
/* ------------------------------------------------------------------ */

uint32_t tuya_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

/* ------------------------------------------------------------------ */
/* AES-128 (FIPS-197), tryb ECB                                        */
/* ------------------------------------------------------------------ */

static const uint8_t SBOX[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16,
};

static const uint8_t INV_SBOX[256] = {
    0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb,
    0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87, 0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb,
    0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d, 0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
    0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2, 0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25,
    0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92,
    0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
    0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a, 0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
    0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02, 0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b,
    0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea, 0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
    0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85, 0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e,
    0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89, 0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b,
    0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
    0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f,
    0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d, 0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
    0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
    0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26, 0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d,
};

static const uint8_t RCON[AES_ROUNDS] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36 };

typedef struct {
    uint8_t rk[(AES_ROUNDS + 1) * AES_BLOCK];
} aes_ctx_t;

static void aes_key_expand(aes_ctx_t *ctx, const uint8_t key[TUYA_KEY_LEN])
{
    memcpy(ctx->rk, key, TUYA_KEY_LEN);
    for (unsigned i = 4; i < 4 * (AES_ROUNDS + 1); i++) {
        uint8_t t[4];
        memcpy(t, &ctx->rk[(i - 1) * 4], 4);
        if (i % 4 == 0) {
            uint8_t tmp = t[0];
            t[0] = (uint8_t)(SBOX[t[1]] ^ RCON[i / 4 - 1]);
            t[1] = SBOX[t[2]];
            t[2] = SBOX[t[3]];
            t[3] = SBOX[tmp];
        }
        for (unsigned j = 0; j < 4; j++)
            ctx->rk[i * 4 + j] = ctx->rk[(i - 4) * 4 + j] ^ t[j];
    }
}

static uint8_t xtime(uint8_t x)
{
    return (uint8_t)((x << 1) ^ ((x & 0x80u) ? 0x1bu : 0x00u));
}

static uint8_t gmul(uint8_t a, uint8_t b)
{
    uint8_t r = 0;
    while (b) {
        if (b & 1u)
            r ^= a;
        a = xtime(a);
        b >>= 1;
    }
    return r;
}

static void add_round_key(uint8_t s[AES_BLOCK], const uint8_t *rk)
{
    for (unsigned i = 0; i < AES_BLOCK; i++)
        s[i] ^= rk[i];
}

/* Stan w kolejności kolumnowej: s[4*c + r] */
static void shift_rows(uint8_t s[AES_BLOCK])
{
    uint8_t t[AES_BLOCK];
    for (unsigned c = 0; c < 4; c++)
        for (unsigned r = 0; r < 4; r++)
            t[4 * c + r] = s[4 * ((c + r) % 4) + r];
    memcpy(s, t, AES_BLOCK);
}

static void inv_shift_rows(uint8_t s[AES_BLOCK])
{
    uint8_t t[AES_BLOCK];
    for (unsigned c = 0; c < 4; c++)
        for (unsigned r = 0; r < 4; r++)
            t[4 * ((c + r) % 4) + r] = s[4 * c + r];
    memcpy(s, t, AES_BLOCK);
}

static void mix_columns(uint8_t s[AES_BLOCK])
{
    for (unsigned c = 0; c < 4; c++) {
        uint8_t *col = &s[4 * c];
        uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
        uint8_t all = a0 ^ a1 ^ a2 ^ a3;
        col[0] ^= all ^ xtime(a0 ^ a1);
        col[1] ^= all ^ xtime(a1 ^ a2);
        col[2] ^= all ^ xtime(a2 ^ a3);
        col[3] ^= all ^ xtime(a3 ^ a0);
    }
}

static void inv_mix_columns(uint8_t s[AES_BLOCK])
{
    for (unsigned c = 0; c < 4; c++) {
        uint8_t *col = &s[4 * c];
        uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
        col[0] = gmul(a0, 14) ^ gmul(a1, 11) ^ gmul(a2, 13) ^ gmul(a3, 9);
        col[1] = gmul(a0, 9) ^ gmul(a1, 14) ^ gmul(a2, 11) ^ gmul(a3, 13);
        col[2] = gmul(a0, 13) ^ gmul(a1, 9) ^ gmul(a2, 14) ^ gmul(a3, 11);
        col[3] = gmul(a0, 11) ^ gmul(a1, 13) ^ gmul(a2, 9) ^ gmul(a3, 14);
    }
}

static void aes_encrypt_block(const aes_ctx_t *ctx, const uint8_t in[AES_BLOCK], uint8_t out[AES_BLOCK])
{
    uint8_t s[AES_BLOCK];
    memcpy(s, in, AES_BLOCK);
    add_round_key(s, ctx->rk);
    for (unsigned round = 1; round <= AES_ROUNDS; round++) {
        for (unsigned i = 0; i < AES_BLOCK; i++)
            s[i] = SBOX[s[i]];
        shift_rows(s);
        if (round != AES_ROUNDS)
            mix_columns(s);
        add_round_key(s, &ctx->rk[round * AES_BLOCK]);
    }
    memcpy(out, s, AES_BLOCK);
}

static void aes_decrypt_block(const aes_ctx_t *ctx, const uint8_t in[AES_BLOCK], uint8_t out[AES_BLOCK])
{
    uint8_t s[AES_BLOCK];
    memcpy(s, in, AES_BLOCK);
    add_round_key(s, &ctx->rk[AES_ROUNDS * AES_BLOCK]);
    for (unsigned round = AES_ROUNDS; round >= 1; round--) {
        inv_shift_rows(s);
        for (unsigned i = 0; i < AES_BLOCK; i++)
            s[i] = INV_SBOX[s[i]];
        add_round_key(s, &ctx->rk[(round - 1) * AES_BLOCK]);
        if (round != 1)
            inv_mix_columns(s);
    }
    memcpy(out, s, AES_BLOCK);
}

int tuya_aes_encrypt(const uint8_t key[TUYA_KEY_LEN], const uint8_t *in, size_t in_len,
                     uint8_t *out, size_t out_cap)
{
    size_t full = in_len / AES_BLOCK;
    size_t out_len = (full + 1) * AES_BLOCK; /* PKCS#7 zawsze dodaje 1..16 bajtów */
    if (out_cap < out_len)
        return TUYA_ERR_BUF_SMALL;

    aes_ctx_t ctx;
    aes_key_expand(&ctx, key);
    for (size_t b = 0; b < full; b++)
        aes_encrypt_block(&ctx, in + b * AES_BLOCK, out + b * AES_BLOCK);

    uint8_t last[AES_BLOCK];
    size_t rem = in_len - full * AES_BLOCK;
    uint8_t pad = (uint8_t)(AES_BLOCK - rem);
    memcpy(last, in + full * AES_BLOCK, rem);
    memset(last + rem, pad, pad);
    aes_encrypt_block(&ctx, last, out + full * AES_BLOCK);
    return (int)out_len;
}

int tuya_aes_decrypt(const uint8_t key[TUYA_KEY_LEN], const uint8_t *in, size_t in_len,
                     uint8_t *out, size_t out_cap)
{
    if (in_len == 0 || in_len % AES_BLOCK != 0)
        return TUYA_ERR_DECRYPT;

    aes_ctx_t ctx;
    aes_key_expand(&ctx, key);

    /* Najpierw ostatni blok – walidacja paddingu i długości wyniku przed zapisem do out */
    size_t last_off = in_len - AES_BLOCK;
    uint8_t last[AES_BLOCK];
    aes_decrypt_block(&ctx, in + last_off, last);
    uint8_t pad = last[AES_BLOCK - 1];
    if (pad == 0 || pad > AES_BLOCK)
        return TUYA_ERR_DECRYPT;
    for (unsigned i = AES_BLOCK - pad; i < AES_BLOCK; i++)
        if (last[i] != pad)
            return TUYA_ERR_DECRYPT;

    size_t plain_len = in_len - pad;
    if (out_cap < plain_len)
        return TUYA_ERR_BUF_SMALL;

    for (size_t off = 0; off < last_off; off += AES_BLOCK)
        aes_decrypt_block(&ctx, in + off, out + off);
    memcpy(out + last_off, last, AES_BLOCK - pad);
    return (int)plain_len;
}

/* ------------------------------------------------------------------ */
/* Ramki                                                               */
/* ------------------------------------------------------------------ */

/* Komendy bez nagłówka "3.3"+12x00 (jak w referencyjnym tinytuya). */
static bool needs_version_header(uint32_t cmd)
{
    switch (cmd) {
    case 3: case 4: case 5: case TUYA_CMD_HEART_BEAT: case TUYA_CMD_DP_QUERY:
    case 16: case 18: case 64:
        return false;
    default:
        return true;
    }
}

int tuya_build_frame(uint32_t seq, uint32_t cmd, const char *json,
                     const uint8_t key[TUYA_KEY_LEN], uint8_t *out, size_t out_cap)
{
    size_t json_len = json ? strlen(json) : 0;
    size_t hdr_len = (json && needs_version_header(cmd)) ? TUYA_VER33_HDR_LEN : 0;
    size_t enc_len = json ? (json_len / AES_BLOCK + 1) * AES_BLOCK : 0;
    size_t payload_len = hdr_len + enc_len;
    size_t total = TUYA_HEADER_LEN + payload_len + TUYA_TRAILER_LEN;
    if (out_cap < total)
        return TUYA_ERR_BUF_SMALL;

    put_be32(out, TUYA_PREFIX);
    put_be32(out + 4, seq);
    put_be32(out + 8, cmd);
    put_be32(out + 12, (uint32_t)(payload_len + TUYA_TRAILER_LEN));

    uint8_t *p = out + TUYA_HEADER_LEN;
    if (hdr_len) {
        memset(p, 0, hdr_len);
        memcpy(p, VER33_HDR, sizeof VER33_HDR);
        p += hdr_len;
    }
    if (json) {
        int n = tuya_aes_encrypt(key, (const uint8_t *)json, json_len, p, enc_len);
        if (n < 0)
            return n;
    }

    size_t crc_off = TUYA_HEADER_LEN + payload_len;
    put_be32(out + crc_off, tuya_crc32(out, crc_off));
    put_be32(out + crc_off + 4, TUYA_SUFFIX);
    return (int)total;
}

int tuya_parse_frame(const uint8_t *buf, size_t len, tuya_frame_t *f)
{
    f->frame_len = 0;
    if (len < TUYA_HEADER_LEN)
        return TUYA_ERR_INCOMPLETE;
    if (get_be32(buf) != TUYA_PREFIX)
        return TUYA_ERR_PREFIX;

    uint32_t body_len = get_be32(buf + 12);
    if (body_len < TUYA_TRAILER_LEN || body_len > TUYA_MAX_FRAME)
        return TUYA_ERR_LEN;

    size_t total = TUYA_HEADER_LEN + body_len;
    if (len < total)
        return TUYA_ERR_INCOMPLETE;
    f->frame_len = total;

    if (get_be32(buf + total - 4) != TUYA_SUFFIX)
        return TUYA_ERR_SUFFIX;
    size_t crc_off = total - TUYA_TRAILER_LEN;
    if (get_be32(buf + crc_off) != tuya_crc32(buf, crc_off))
        return TUYA_ERR_CRC;

    f->seq = get_be32(buf + 4);
    f->cmd = get_be32(buf + 8);

    const uint8_t *body = buf + TUYA_HEADER_LEN;
    size_t data_len = body_len - TUYA_TRAILER_LEN;
    f->has_retcode = data_len >= 4 && (get_be32(body) & 0xFFFFFF00u) == 0;
    if (f->has_retcode) {
        f->retcode = get_be32(body);
        body += 4;
        data_len -= 4;
    } else {
        f->retcode = 0;
    }
    f->payload = body;
    f->payload_len = data_len;
    return TUYA_OK;
}

int tuya_decode_payload(const tuya_frame_t *f, const uint8_t key[TUYA_KEY_LEN],
                        char *json_out, size_t cap)
{
    if (cap == 0)
        return TUYA_ERR_BUF_SMALL;

    const uint8_t *p = f->payload;
    size_t n = f->payload_len;
    /* tylko "3.3" – urządzenia wpisują w pozostałe 12 bajtów nagłówka m.in. numer sekwencji */
    if (n >= TUYA_VER33_HDR_LEN && memcmp(p, VER33_HDR, 3) == 0) {
        p += TUYA_VER33_HDR_LEN;
        n -= TUYA_VER33_HDR_LEN;
    }
    if (n == 0) {
        json_out[0] = '\0';
        return 0;
    }

    int r = tuya_aes_decrypt(key, p, n, (uint8_t *)json_out, cap - 1);
    if (r < 0)
        return r;
    json_out[r] = '\0';
    return r;
}

int tuya_decode_discovery(const uint8_t *buf, size_t len, char *json_out, size_t cap)
{
    tuya_frame_t f;
    int r = tuya_parse_frame(buf, len, &f);
    if (r != TUYA_OK)
        return r;
    return tuya_decode_payload(&f, UDP_KEY, json_out, cap);
}
