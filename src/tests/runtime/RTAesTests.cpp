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
#include "rt_string.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void check(const char *label, int ok)
{
    printf("  %-50s %s\n", label, ok ? "PASS" : "FAIL");
    assert(ok);
}

static void obj_release(void *obj)
{
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

static rt_string S(const char *s)
{
    return rt_string_from_bytes(s, strlen(s));
}

static void test_str_roundtrip(void)
{
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

static void test_empty_str_roundtrip(void)
{
    printf("rt_aes_encrypt_str / rt_aes_decrypt_str with empty string:\n");

    rt_string plaintext = rt_string_from_bytes("", 0);
    rt_string password = S("pw");

    void *ciphertext = rt_aes_encrypt_str(plaintext, password);
    check("encrypt empty string returns non-null", ciphertext != NULL);

    rt_string decrypted = rt_aes_decrypt_str(ciphertext, password);
    check("decrypt empty roundtrip length is 0", rt_str_len(decrypted) == 0);

    rt_string_unref(decrypted);
    obj_release(ciphertext);
    rt_string_unref(password);
    rt_string_unref(plaintext);
}

static void test_raw_aes128_roundtrip(void)
{
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
    for (int64_t i = 0; i < 20; ++i)
    {
        if (rt_bytes_get(decrypted, i) != rt_bytes_get(orig, i))
        {
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

int main(void)
{
    printf("=== RTAesTests ===\n");
    test_str_roundtrip();
    test_empty_str_roundtrip();
    // wrong-password decrypt traps (bad PKCS7 padding aborts via rt_trap),
    // so that path cannot be tested with assert-style checks.
    test_raw_aes128_roundtrip();
    printf("All AES tests passed.\n");
    return 0;
}
