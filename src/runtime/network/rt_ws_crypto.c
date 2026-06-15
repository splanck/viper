//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_ws_crypto.c
// Purpose: WebSocket handshake crypto/encoding: UTF-8 validation, SHA-1, the
//   client nonce generator, and the Sec-WebSocket-Accept key computation.
//
// Links: rt_websocket.h, rt_websocket_internal.h, rt_websocket.c
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//

#include "rt_websocket.h"

#include "rt_bytes.h"
#include "rt_crypto.h"
#include "rt_object.h"
#include "rt_random.h"
#include "rt_string.h"
#include "rt_tls.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward declarations (defined in rt_io.c).
#include "rt_trap.h"
extern void rt_trap_net(const char *msg, int err_code);

#include "rt_error.h"

#include "rt_websocket_internal.h"

int ws_is_valid_utf8(const uint8_t *data, size_t len) {
    size_t i = 0;
    while (i < len) {
        uint8_t c = data[i++];
        if (c <= 0x7F)
            continue;

        if (c >= 0xC2 && c <= 0xDF) {
            if (i >= len || (data[i] & 0xC0) != 0x80)
                return 0;
            i++;
            continue;
        }

        if (c == 0xE0) {
            if (i + 1 >= len || data[i] < 0xA0 || data[i] > 0xBF || (data[i + 1] & 0xC0) != 0x80)
                return 0;
            i += 2;
            continue;
        }

        if (c >= 0xE1 && c <= 0xEC) {
            if (i + 1 >= len || (data[i] & 0xC0) != 0x80 || (data[i + 1] & 0xC0) != 0x80)
                return 0;
            i += 2;
            continue;
        }

        if (c == 0xED) {
            if (i + 1 >= len || data[i] < 0x80 || data[i] > 0x9F || (data[i + 1] & 0xC0) != 0x80)
                return 0;
            i += 2;
            continue;
        }

        if (c >= 0xEE && c <= 0xEF) {
            if (i + 1 >= len || (data[i] & 0xC0) != 0x80 || (data[i + 1] & 0xC0) != 0x80)
                return 0;
            i += 2;
            continue;
        }

        if (c == 0xF0) {
            if (i + 2 >= len || data[i] < 0x90 || data[i] > 0xBF || (data[i + 1] & 0xC0) != 0x80 ||
                (data[i + 2] & 0xC0) != 0x80)
                return 0;
            i += 3;
            continue;
        }

        if (c >= 0xF1 && c <= 0xF3) {
            if (i + 2 >= len || (data[i] & 0xC0) != 0x80 || (data[i + 1] & 0xC0) != 0x80 ||
                (data[i + 2] & 0xC0) != 0x80)
                return 0;
            i += 3;
            continue;
        }

        if (c == 0xF4) {
            if (i + 2 >= len || data[i] < 0x80 || data[i] > 0x8F || (data[i + 1] & 0xC0) != 0x80 ||
                (data[i + 2] & 0xC0) != 0x80)
                return 0;
            i += 3;
            continue;
        }

        return 0;
    }
    return 1;
}

/// @brief Minimal SHA-1 (RFC 3174) for Sec-WebSocket-Accept validation (RFC 6455 §4.1).
/// SHA-1 is acceptable here: it is used as a protocol-mandated HMAC-like check,
/// not for general cryptographic security.
int ws_sha1(const uint8_t *data, size_t len, uint8_t digest[20]) {
    uint32_t h0 = 0x67452301u, h1 = 0xEFCDAB89u, h2 = 0x98BADCFEu;
    uint32_t h3 = 0x10325476u, h4 = 0xC3D2E1F0u;

    size_t padded_len = ((len + 9 + 63) / 64) * 64;
    uint8_t *padded = (uint8_t *)calloc(padded_len, 1);
    if (!padded)
        return 0;
    memcpy(padded, data, len);
    padded[len] = 0x80;
    uint64_t bit_len = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++)
        padded[padded_len - 8 + i] = (uint8_t)(bit_len >> (56 - i * 8));

    for (size_t chunk = 0; chunk < padded_len; chunk += 64) {
        uint32_t w[80];
        for (int j = 0; j < 16; j++) {
            w[j] = ((uint32_t)padded[chunk + j * 4] << 24) |
                   ((uint32_t)padded[chunk + j * 4 + 1] << 16) |
                   ((uint32_t)padded[chunk + j * 4 + 2] << 8) |
                   ((uint32_t)padded[chunk + j * 4 + 3]);
        }
        for (int j = 16; j < 80; j++) {
            uint32_t t = w[j - 3] ^ w[j - 8] ^ w[j - 14] ^ w[j - 16];
            w[j] = (t << 1) | (t >> 31);
        }
        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int j = 0; j < 80; j++) {
            uint32_t f, k;
            if (j < 20) {
                f = (b & c) | (~b & d);
                k = 0x5A827999u;
            } else if (j < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1u;
            } else if (j < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDCu;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6u;
            }
            uint32_t temp = ((a << 5) | (a >> 27)) + f + e + k + w[j];
            e = d;
            d = c;
            c = (b << 30) | (b >> 2);
            b = a;
            a = temp;
        }
        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }
    free(padded);

    for (int i = 0; i < 4; i++) {
        digest[i] = (uint8_t)(h0 >> (24 - i * 8));
        digest[4 + i] = (uint8_t)(h1 >> (24 - i * 8));
        digest[8 + i] = (uint8_t)(h2 >> (24 - i * 8));
        digest[12 + i] = (uint8_t)(h3 >> (24 - i * 8));
        digest[16 + i] = (uint8_t)(h4 >> (24 - i * 8));
    }
    return 1;
}

/// @brief Generate a random WebSocket key (16 random bytes, base64 encoded).
rt_string generate_ws_key(void) {
    // Generate 16 cryptographically-random bytes (RFC 6455 §4.1 requires unpredictability)
    uint8_t raw[16];
    rt_crypto_random_bytes(raw, sizeof(raw));

    void *bytes = rt_bytes_new(16);
    for (int i = 0; i < 16; i++)
        rt_bytes_set(bytes, i, raw[i]);

    // Encode to base64
    rt_string result = rt_bytes_to_base64(bytes);
    if (rt_obj_release_check0(bytes))
        rt_obj_free(bytes);
    return result;
}

/// @brief Compute the Sec-WebSocket-Accept header value for a given key.
///
/// Returns Base64(SHA1(key + WS_MAGIC)) as a malloc'd C string.
/// The caller is responsible for freeing the returned string.
/// Returns NULL on allocation failure.
char *rt_ws_compute_accept_key(const char *key_cstr) {
    if (!key_cstr)
        return NULL;

    static const char WS_MAGIC[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    size_t key_len = strlen(key_cstr);
    size_t magic_len = sizeof(WS_MAGIC) - 1;
    if (key_len > SIZE_MAX - magic_len - 1)
        return NULL;

    char *concat = (char *)malloc(key_len + magic_len + 1);
    if (!concat)
        return NULL;

    memcpy(concat, key_cstr, key_len);
    memcpy(concat + key_len, WS_MAGIC, magic_len);
    concat[key_len + magic_len] = '\0';

    uint8_t sha1_digest[20];
    if (!ws_sha1((const uint8_t *)concat, key_len + magic_len, sha1_digest)) {
        free(concat);
        return NULL;
    }
    free(concat);

    // Base64-encode using rt_bytes_to_base64
    void *digest_bytes = rt_bytes_new(20);
    if (!digest_bytes)
        return NULL;

    for (int i = 0; i < 20; i++)
        rt_bytes_set(digest_bytes, i, sha1_digest[i]);

    rt_string accept_str = rt_bytes_to_base64(digest_bytes);
    if (rt_obj_release_check0(digest_bytes))
        rt_obj_free(digest_bytes);

    if (!accept_str)
        return NULL;

    const char *accept_cstr = rt_string_cstr(accept_str);
    char *result = accept_cstr ? strdup(accept_cstr) : NULL;
    rt_string_unref(accept_str);
    return result;
}
