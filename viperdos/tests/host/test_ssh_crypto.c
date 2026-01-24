/**
 * @file test_ssh_crypto.c
 * @brief Host-side known-answer tests for user/libssh crypto primitives.
 */

#include "test_framework.h"

#include <stdint.h>
#include <string.h>
#include <sys/types.h>

#include "../../user/libssh/ssh_internal.h"

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F')
        return 10 + (c - 'A');
    return -1;
}

static int hex_decode(const char *hex, uint8_t *out, size_t out_len) {
    size_t hex_len = strlen(hex);
    if ((hex_len % 2) != 0)
        return -1;
    if ((hex_len / 2) != out_len)
        return -1;

    for (size_t i = 0; i < out_len; i++) {
        int hi = hex_nibble(hex[i * 2]);
        int lo = hex_nibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0)
            return -1;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return 0;
}

static int bytes_eq(const uint8_t *a, const uint8_t *b, size_t len) {
    uint8_t diff = 0;
    for (size_t i = 0; i < len; i++) {
        diff |= (uint8_t)(a[i] ^ b[i]);
    }
    return diff == 0;
}

int main(void) {
    TEST_BEGIN("ssh_crypto known-answer tests");

    TEST_SECTION("SHA-256");
    {
        static const uint8_t expected[32] = {
            0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
            0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17,
            0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad,
        };
        uint8_t digest[32];
        ssh_sha256("abc", 3, digest);
        TEST_ASSERT(bytes_eq(digest, expected, sizeof(expected)), "SHA256('abc')");
    }

    TEST_SECTION("HMAC-SHA256 (RFC 4231 test case 1)");
    {
        uint8_t key[20];
        memset(key, 0x0b, sizeof(key));
        static const uint8_t expected[32] = {
            0xb0, 0x34, 0x4c, 0x61, 0xd8, 0xdb, 0x38, 0x53, 0x5c, 0xa8, 0xaf,
            0xce, 0xaf, 0x0b, 0xf1, 0x2b, 0x88, 0x1d, 0xc2, 0x00, 0xc9, 0x83,
            0x3d, 0xa7, 0x26, 0xe9, 0x37, 0x6c, 0x2e, 0x32, 0xcf, 0xf7,
        };
        uint8_t mac[32];
        ssh_hmac_sha256(key, sizeof(key), "Hi There", 8, mac);
        TEST_ASSERT(bytes_eq(mac, expected, sizeof(expected)), "HMAC-SHA256");
    }

    TEST_SECTION("X25519 (RFC 7748 §5.2)");
    {
        uint8_t scalar[32];
        uint8_t u_in[32];
        uint8_t expected_u_out[32];
        uint8_t u_out[32];

        TEST_ASSERT(hex_decode("a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4",
                               scalar,
                               sizeof(scalar)) == 0,
                    "decode scalar #1");
        TEST_ASSERT(hex_decode("e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c",
                               u_in,
                               sizeof(u_in)) == 0,
                    "decode u-coordinate #1");
        TEST_ASSERT(hex_decode("c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552",
                               expected_u_out,
                               sizeof(expected_u_out)) == 0,
                    "decode expected u-coordinate #1");

        ssh_x25519(scalar, u_in, u_out);
        TEST_ASSERT(bytes_eq(u_out, expected_u_out, sizeof(u_out)), "X25519 test vector #1");
    }

    TEST_SECTION("AES-CTR (NIST SP 800-38A F.5.1)");
    {
        uint8_t key[16];
        uint8_t iv[16];
        uint8_t plaintext[64];
        uint8_t expected_ciphertext[64];
        uint8_t ciphertext[64];
        ssh_cipher_ctx_t ctx;

        TEST_ASSERT(hex_decode("2b7e151628aed2a6abf7158809cf4f3c", key, sizeof(key)) == 0,
                    "decode AES-128 key");
        TEST_ASSERT(hex_decode("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff", iv, sizeof(iv)) == 0,
                    "decode AES-CTR IV");
        TEST_ASSERT(hex_decode("6bc1bee22e409f96e93d7e117393172a"
                               "ae2d8a571e03ac9c9eb76fac45af8e51"
                               "30c81c46a35ce411e5fbc1191a0a52ef"
                               "f69f2445df4f9b17ad2b417be66c3710",
                               plaintext,
                               sizeof(plaintext)) == 0,
                    "decode AES-CTR plaintext");
        TEST_ASSERT(hex_decode("874d6191b620e3261bef6864990db6ce"
                               "9806f66b7970fdff8617187bb9fffdff"
                               "5ae4df3edbd5d35e5b4f09020db03eab"
                               "1e031dda2fbe03d1792170a0f3009cee",
                               expected_ciphertext,
                               sizeof(expected_ciphertext)) == 0,
                    "decode AES-CTR expected ciphertext");

        memset(&ctx, 0, sizeof(ctx));
        ssh_aes_ctr_init(&ctx, key, sizeof(key), iv);
        ssh_aes_ctr_process(&ctx, plaintext, ciphertext, sizeof(ciphertext));

        TEST_ASSERT(bytes_eq(ciphertext, expected_ciphertext, sizeof(ciphertext)),
                    "AES-CTR test vector");
    }

    TEST_END();
}
