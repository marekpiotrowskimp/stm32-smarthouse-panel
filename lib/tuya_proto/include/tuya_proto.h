/*
 * Protokół lokalny Tuya 3.3 – kodowanie/dekodowanie ramek, AES-128-ECB, CRC32.
 * Biblioteka bez zależności sprzętowych i bez alokacji dynamicznej (docs/SPEC.md §5).
 *
 * Ramka (big-endian):
 *   000055AA | seq u32 | cmd u32 | len u32 | [retcode u32] payload | crc32 u32 | 0000AA55
 *   len   = bajty od pola retcode/payload do sufiksu włącznie
 *   crc32 = CRC-32 (IEEE) od prefiksu do końca payloadu
 */
#ifndef TUYA_PROTO_H
#define TUYA_PROTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TUYA_CMD_CONTROL    7u
#define TUYA_CMD_STATUS     8u
#define TUYA_CMD_HEART_BEAT 9u
#define TUYA_CMD_DP_QUERY   10u

#define TUYA_KEY_LEN        16u
#define TUYA_HEADER_LEN     16u   /* prefix + seq + cmd + len */
#define TUYA_TRAILER_LEN    8u    /* crc32 + suffix */
#define TUYA_VER33_HDR_LEN  15u   /* "3.3" + 12 x 0x00 */
#define TUYA_MAX_FRAME      4096u /* większe pole len = ramka uszkodzona */

typedef enum {
    TUYA_OK             = 0,
    TUYA_ERR_BUF_SMALL  = -1, /* bufor wyjściowy za mały */
    TUYA_ERR_INCOMPLETE = -2, /* za mało bajtów – czekaj na resztę strumienia */
    TUYA_ERR_PREFIX     = -3, /* brak 000055AA na początku */
    TUYA_ERR_SUFFIX     = -4, /* brak 0000AA55 na końcu */
    TUYA_ERR_CRC        = -5, /* niezgodne CRC32 */
    TUYA_ERR_LEN        = -6, /* pole len poza zakresem */
    TUYA_ERR_DECRYPT    = -7, /* długość nie jest wielokrotnością 16 lub błędny padding (zły klucz) */
} tuya_err_t;

typedef struct {
    uint32_t       seq;
    uint32_t       cmd;
    bool           has_retcode;  /* pierwsze 4 bajty po nagłówku mają postać 0x000000XX */
    uint32_t       retcode;
    const uint8_t *payload;      /* wskazuje do bufora wejściowego, za retcode */
    size_t         payload_len;
    size_t         frame_len;    /* długość całej ramki – ustawiana także przy CRC/SUFFIX, by pominąć ramkę */
} tuya_frame_t;

/* CRC-32 IEEE 802.3 (jak zlib.crc32). */
uint32_t tuya_crc32(const uint8_t *data, size_t len);

/* AES-128-ECB z paddingiem PKCS#7. Zwraca długość wyniku lub tuya_err_t (<0). */
int tuya_aes_encrypt(const uint8_t key[TUYA_KEY_LEN], const uint8_t *in, size_t in_len,
                     uint8_t *out, size_t out_cap);
int tuya_aes_decrypt(const uint8_t key[TUYA_KEY_LEN], const uint8_t *in, size_t in_len,
                     uint8_t *out, size_t out_cap);

/*
 * Buduje kompletną ramkę do wysłania.
 *   CONTROL      : payload = "3.3"+12x00 + AES(json)
 *   DP_QUERY     : payload = AES(json)
 *   HEART_BEAT   : json == NULL -> pusty payload
 * Zwraca długość ramki lub tuya_err_t (<0).
 */
int tuya_build_frame(uint32_t seq, uint32_t cmd, const char *json,
                     const uint8_t key[TUYA_KEY_LEN], uint8_t *out, size_t out_cap);

/*
 * Parsuje jedną ramkę z początku bufora strumienia TCP/UDP.
 * TUYA_OK – ramka poprawna, f wypełnione; TUYA_ERR_INCOMPLETE – potrzeba więcej bajtów.
 */
int tuya_parse_frame(const uint8_t *buf, size_t len, tuya_frame_t *f);

/*
 * Odszyfrowuje payload odebranej ramki do JSON zakończonego '\0'
 * (nagłówek "3.3"+12x00, jeśli jest, zostaje pominięty).
 * Zwraca długość JSON (bez '\0') lub tuya_err_t (<0).
 */
int tuya_decode_payload(const tuya_frame_t *f, const uint8_t key[TUYA_KEY_LEN],
                        char *json_out, size_t cap);

/* Dekoduje broadcast UDP 6667 (klucz md5("yGAdlopoPVldABfn")) do JSON. Zwraca długość lub tuya_err_t. */
int tuya_decode_discovery(const uint8_t *buf, size_t len, char *json_out, size_t cap);

#endif /* TUYA_PROTO_H */
