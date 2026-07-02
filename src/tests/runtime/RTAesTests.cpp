//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTAesTests.cpp
// Purpose: Validate AES-CBC encrypt/decrypt roundtrip via rt_aes_* API.
// Key invariants: encrypt then decrypt with the same key+IV recovers plaintext;
//                 AES-256-CBC string API roundtrips with password derivation.
// Ownership/Lifetime: Returned Bytes objects managed via rt_obj_release_check0.
//
//===----------------------------------------------------------------------===//

#include "viper/runtime/rt.h"

#include "rt_aes.h"
#include "rt_bytes.h"
#include "rt_option.h"
#include "rt_result.h"
#include "rt_string.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void check(const char *label, int ok) {
    printf("  %-50s %s\n", label, ok ? "PASS" : "FAIL");
    assert(ok);
}

static void obj_release(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

static rt_string S(const char *s) {
    return rt_string_from_bytes(s, strlen(s));
}

static rt_string S_raw(const char *s, size_t len) {
    return rt_string_from_bytes(s, len);
}

static void *B_raw(const uint8_t *data, size_t len) {
    void *bytes = rt_bytes_new((int64_t)len);
    for (size_t i = 0; i < len; i++)
        rt_bytes_set(bytes, (int64_t)i, data[i]);
    return bytes;
}

static void test_str_roundtrip(void) {
    printf("rt_aes_encrypt_str / rt_aes_decrypt_str roundtrip:\n");

    rt_string plaintext = S("Hello, AES!");
    rt_string password = S("secret-password-123");

    void *ciphertext = rt_aes_encrypt_str(plaintext, password);
    check("encrypt returns non-null", ciphertext != NULL);

    rt_string decrypted = rt_aes_decrypt_str(ciphertext, password);
    check("decrypt is non-empty", rt_str_len(decrypted) > 0);

    rt_string expected = S("Hello, AES!");
    check("decrypted equals original", rt_str_eq(decrypted, expected));

    rt_string_unref(expected);
    rt_string_unref(decrypted);
    obj_release(ciphertext);
    rt_string_unref(password);
    rt_string_unref(plaintext);
}

static void test_empty_str_roundtrip(void) {
    printf("rt_aes_encrypt_str / rt_aes_decrypt_str with empty string:\n");

    rt_string plaintext = rt_string_from_bytes("", 0);
    rt_string password = S("pw");

    void *ciphertext = rt_aes_encrypt_str(plaintext, password);
    check("encrypt empty string returns non-null", ciphertext != NULL);

    rt_string decrypted = rt_aes_decrypt_str(ciphertext, password);
    check("decrypt empty roundtrip length is 0", rt_str_len(decrypted) == 0);

    void *result = rt_aes_decrypt_str_result(ciphertext, password);
    check("DecryptStrResult empty string is Ok", rt_result_is_ok(result) == 1);
    rt_string result_text = rt_result_unwrap_str(result);
    check("DecryptStrResult empty string length is 0", rt_str_len(result_text) == 0);

    rt_string_unref(decrypted);
    obj_release(ciphertext);
    rt_string_unref(password);
    rt_string_unref(plaintext);
}

static void test_embedded_nul_str_roundtrip(void) {
    printf("rt_aes_encrypt_str / rt_aes_decrypt_str with embedded NUL bytes:\n");

    const char plain_raw[] = {'A', 0, 'B'};
    const char pass_raw[] = {'p', 0, 'w'};
    rt_string plaintext = S_raw(plain_raw, sizeof(plain_raw));
    rt_string password = S_raw(pass_raw, sizeof(pass_raw));

    void *ciphertext = rt_aes_encrypt_str(plaintext, password);
    check("encrypt embedded NUL string returns non-null", ciphertext != NULL);

    rt_string decrypted = rt_aes_decrypt_str(ciphertext, password);
    check("decrypt embedded NUL length", rt_str_len(decrypted) == (int64_t)sizeof(plain_raw));
    check("decrypt embedded NUL bytes",
          memcmp(rt_string_cstr(decrypted), plain_raw, sizeof(plain_raw)) == 0);

    rt_string_unref(decrypted);
    obj_release(ciphertext);
    rt_string_unref(password);
    rt_string_unref(plaintext);
}

static void test_str_format_magic(void) {
    printf("rt_aes_encrypt_str emits authenticated VAG1 payload:\n");

    rt_string plaintext = S("Format probe");
    rt_string password = S("format-password");

    void *ciphertext = rt_aes_encrypt_str(plaintext, password);
    check("ciphertext non-null", ciphertext != NULL);
    check("ciphertext length >= header + tag", rt_bytes_len(ciphertext) >= 36 + 16);
    check("magic[0] == 'V'", rt_bytes_get(ciphertext, 0) == 'V');
    check("magic[1] == 'A'", rt_bytes_get(ciphertext, 1) == 'A');
    check("magic[2] == 'G'", rt_bytes_get(ciphertext, 2) == 'G');
    check("magic[3] == '1'", rt_bytes_get(ciphertext, 3) == '1');

    obj_release(ciphertext);
    rt_string_unref(password);
    rt_string_unref(plaintext);
}

static void test_raw_aes128_roundtrip(void) {
    printf("rt_aes_encrypt / rt_aes_decrypt AES-128 (from hex keys):\n");

    // 16-byte key: 2b7e151628aed2a6abf7158809cf4f3c
    rt_string key_hex = S("2b7e151628aed2a6abf7158809cf4f3c");
    void *key = rt_bytes_from_hex(key_hex);
    rt_string_unref(key_hex);
    check("key length == 16", rt_bytes_len(key) == 16);

    // 16-byte IV: 000102030405060708090a0b0c0d0e0f
    rt_string iv_hex = S("000102030405060708090a0b0c0d0e0f");
    void *iv = rt_bytes_from_hex(iv_hex);
    rt_string_unref(iv_hex);

    // 16-byte plaintext: "AES-128 test!!!!  " (16 chars)
    rt_string data_hex = S("4145532d31323820746573742121212121212121");
    void *data = rt_bytes_from_hex(data_hex);
    rt_string_unref(data_hex);
    check("data length == 20", rt_bytes_len(data) == 20);

    void *encrypted = rt_aes_encrypt(data, key, iv);
    check("encrypt returns non-null", encrypted != NULL);
    // Ciphertext with PKCS7 padding is always a multiple of 16
    check("ciphertext len is multiple of 16", rt_bytes_len(encrypted) % 16 == 0);

    // Re-create IV for decrypt (rt_aes_encrypt may consume it)
    rt_string iv2_hex = S("000102030405060708090a0b0c0d0e0f");
    void *iv2 = rt_bytes_from_hex(iv2_hex);
    rt_string_unref(iv2_hex);

    void *decrypted = rt_aes_decrypt(encrypted, key, iv2);
    check("decrypt returns non-null", decrypted != NULL);
    check("decrypted length == 20", rt_bytes_len(decrypted) == 20);

    // Verify decrypted bytes match original
    int match = 1;
    void *orig_bytes = data; // still valid (not consumed)
    // Re-create original data for comparison since data may be consumed
    rt_string orig_hex = S("4145532d31323820746573742121212121212121");
    void *orig = rt_bytes_from_hex(orig_hex);
    rt_string_unref(orig_hex);
    for (int64_t i = 0; i < 20; ++i) {
        if (rt_bytes_get(decrypted, i) != rt_bytes_get(orig, i)) {
            match = 0;
            break;
        }
    }
    check("decrypted bytes match original", match);

    obj_release(orig);
    obj_release(decrypted);
    obj_release(iv2);
    obj_release(encrypted);
    obj_release(data);
    obj_release(iv);
    obj_release(key);
    (void)orig_bytes;
}

static void test_raw_aes128_invalid_padding_returns_null(void) {
    printf("rt_aes_decrypt returns NULL for invalid CBC padding:\n");

    rt_string key_hex = S("2b7e151628aed2a6abf7158809cf4f3c");
    void *key = rt_bytes_from_hex(key_hex);
    rt_string_unref(key_hex);

    rt_string iv_hex = S("000102030405060708090a0b0c0d0e0f");
    void *iv = rt_bytes_from_hex(iv_hex);
    rt_string_unref(iv_hex);

    rt_string data_hex = S("4145532d31323820746573742121212121212121");
    void *data = rt_bytes_from_hex(data_hex);
    rt_string_unref(data_hex);

    void *encrypted = rt_aes_encrypt(data, key, iv);
    check("encrypt returns non-null", encrypted != NULL);
    int64_t enc_len = rt_bytes_len(encrypted);
    check("ciphertext not empty", enc_len > 0);
    uint8_t last = (uint8_t)rt_bytes_get(encrypted, enc_len - 1);
    rt_bytes_set(encrypted, enc_len - 1, (uint8_t)(last ^ 0x01));

    rt_string iv2_hex = S("000102030405060708090a0b0c0d0e0f");
    void *iv2 = rt_bytes_from_hex(iv2_hex);
    rt_string_unref(iv2_hex);

    void *decrypted = rt_aes_decrypt(encrypted, key, iv2);
    check("invalid padding returns NULL", decrypted == NULL);

    rt_string iv3_hex = S("000102030405060708090a0b0c0d0e0f");
    void *iv3 = rt_bytes_from_hex(iv3_hex);
    rt_string_unref(iv3_hex);
    void *bad_result = rt_aes_decrypt_result(encrypted, key, iv3);
    check("invalid padding DecryptResult returns Err", rt_result_is_err(bad_result) == 1);

    rt_string iv4_hex = S("000102030405060708090a0b0c0d0e0f");
    void *iv4 = rt_bytes_from_hex(iv4_hex);
    rt_string_unref(iv4_hex);
    void *bad_option = rt_aes_try_decrypt(encrypted, key, iv4);
    check("invalid padding TryDecrypt returns None", rt_option_is_none(bad_option) == 1);

    obj_release(iv4);
    obj_release(iv3);
    obj_release(iv2);
    obj_release(encrypted);
    obj_release(data);
    obj_release(iv);
    obj_release(key);
}

static void test_auth_malformed_frame_returns_null(void) {
    printf("rt_aes_decrypt_auth returns NULL for malformed frames:\n");

    uint8_t key_raw[16];
    memset(key_raw, 0x42, sizeof(key_raw));
    void *key = B_raw(key_raw, sizeof(key_raw));

    uint8_t malformed[32];
    memset(malformed, 0, sizeof(malformed));
    malformed[0] = 'B';
    malformed[1] = 'A';
    malformed[2] = 'D';
    malformed[3] = '!';
    void *ciphertext = B_raw(malformed, sizeof(malformed));

    void *decrypted = rt_aes_decrypt_auth(ciphertext, key, NULL);
    check("malformed auth frame returns NULL", decrypted == NULL);

    void *bad_result = rt_aes_decrypt_auth_result(ciphertext, key, NULL);
    check("malformed auth frame DecryptAuthResult returns Err", rt_result_is_err(bad_result) == 1);

    void *bad_option = rt_aes_try_decrypt_auth(ciphertext, key, NULL);
    check("malformed auth frame TryDecryptAuth returns None", rt_option_is_none(bad_option) == 1);

    obj_release(ciphertext);
    obj_release(key);
}

int main(void) {
    printf("=== RTAesTests ===\n");
    test_str_roundtrip();
    test_empty_str_roundtrip();
    test_embedded_nul_str_roundtrip();
    test_str_format_magic();
    test_raw_aes128_roundtrip();
    test_raw_aes128_invalid_padding_returns_null();
    test_auth_malformed_frame_returns_null();
    printf("All AES tests passed.\n");
    return 0;
}
