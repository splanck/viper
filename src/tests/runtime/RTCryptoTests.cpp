//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTCryptoTests.cpp
// Purpose: Validate HMAC, PBKDF2, and secure random functions.
// Key invariants: Results match known test vectors (RFC 2202, RFC 6070).
// Links: docs/viperlib/crypto.md
//
//===----------------------------------------------------------------------===//

#include "rt_bytes.h"
#include "rt_aes.h"
#include "rt_cipher.h"
#include "rt_crypto.h"
#include "rt_hash.h"
#include "rt_keyderive.h"
#include "rt_password.h"
#include "rt_rand.h"
#include "rt_string.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>
#include <set>

/// @brief Helper to print test result.
static void test_result(const char *name, bool passed) {
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
    assert(passed);
}

/// @brief Create a Bytes object from raw data.
static void *make_bytes(const uint8_t *data, size_t len) {
    void *bytes = rt_bytes_new((int64_t)len);
    for (size_t i = 0; i < len; i++) {
        rt_bytes_set(bytes, (int64_t)i, data[i]);
    }
    return bytes;
}

/// @brief Create a Bytes object from a C string.
static void *make_bytes_str(const char *str) {
    return make_bytes((const uint8_t *)str, strlen(str));
}

static rt_string make_string_raw(const uint8_t *data, size_t len) {
    return rt_string_from_bytes((const char *)data, len);
}

static bool bytes_equal(void *bytes, const uint8_t *expected, size_t len) {
    if (!bytes || rt_bytes_len(bytes) != (int64_t)len)
        return false;
    for (size_t i = 0; i < len; i++) {
        if ((uint8_t)rt_bytes_get(bytes, (int64_t)i) != expected[i])
            return false;
    }
    return true;
}

//=============================================================================
// HMAC-MD5 Tests (RFC 2202)
//=============================================================================

static void test_hmac_md5() {
    printf("Testing Hash.HmacMD5:\n");

    // Test 1: key = 0x0b repeated 16 times, data = "Hi There"
    // Expected: 9294727a3638bb1c13f48ef8158bfc9d
    {
        uint8_t key_data[16];
        memset(key_data, 0x0b, 16);
        void *key = make_bytes(key_data, 16);
        void *data = make_bytes_str("Hi There");

        rt_string result = rt_hash_hmac_md5_bytes(key, data);
        test_result("HMAC-MD5 Test 1",
                    strcmp(rt_string_cstr(result), "9294727a3638bb1c13f48ef8158bfc9d") == 0);
    }

    // Test 2: key = "Jefe", data = "what do ya want for nothing?"
    // Expected: 750c783e6ab0b503eaa86e310a5db738
    {
        rt_string key = rt_const_cstr("Jefe");
        rt_string data = rt_const_cstr("what do ya want for nothing?");

        rt_string result = rt_hash_hmac_md5(key, data);
        test_result("HMAC-MD5 Test 2",
                    strcmp(rt_string_cstr(result), "750c783e6ab0b503eaa86e310a5db738") == 0);
    }

    // Test 3: key = 0xaa repeated 16 times, data = 0xdd repeated 50 times
    // Expected: 56be34521d144c88dbb8c733f0e8b3f6
    {
        uint8_t key_data[16];
        memset(key_data, 0xaa, 16);
        uint8_t data_bytes[50];
        memset(data_bytes, 0xdd, 50);

        void *key = make_bytes(key_data, 16);
        void *data = make_bytes(data_bytes, 50);

        rt_string result = rt_hash_hmac_md5_bytes(key, data);
        test_result("HMAC-MD5 Test 3",
                    strcmp(rt_string_cstr(result), "56be34521d144c88dbb8c733f0e8b3f6") == 0);
    }

    printf("\n");
}

//=============================================================================
// HMAC-SHA1 Tests (RFC 2202)
//=============================================================================

static void test_hmac_sha1() {
    printf("Testing Hash.HmacSHA1:\n");

    // Test 1: key = 0x0b repeated 20 times, data = "Hi There"
    // Expected: b617318655057264e28bc0b6fb378c8ef146be00
    {
        uint8_t key_data[20];
        memset(key_data, 0x0b, 20);
        void *key = make_bytes(key_data, 20);
        void *data = make_bytes_str("Hi There");

        rt_string result = rt_hash_hmac_sha1_bytes(key, data);
        test_result("HMAC-SHA1 Test 1",
                    strcmp(rt_string_cstr(result), "b617318655057264e28bc0b6fb378c8ef146be00") ==
                        0);
    }

    // Test 2: key = "Jefe", data = "what do ya want for nothing?"
    // Expected: effcdf6ae5eb2fa2d27416d5f184df9c259a7c79
    {
        rt_string key = rt_const_cstr("Jefe");
        rt_string data = rt_const_cstr("what do ya want for nothing?");

        rt_string result = rt_hash_hmac_sha1(key, data);
        test_result("HMAC-SHA1 Test 2",
                    strcmp(rt_string_cstr(result), "effcdf6ae5eb2fa2d27416d5f184df9c259a7c79") ==
                        0);
    }

    // Test 3: key = 0xaa repeated 20 times, data = 0xdd repeated 50 times
    // Expected: 125d7342b9ac11cd91a39af48aa17b4f63f175d3
    {
        uint8_t key_data[20];
        memset(key_data, 0xaa, 20);
        uint8_t data_bytes[50];
        memset(data_bytes, 0xdd, 50);

        void *key = make_bytes(key_data, 20);
        void *data = make_bytes(data_bytes, 50);

        rt_string result = rt_hash_hmac_sha1_bytes(key, data);
        test_result("HMAC-SHA1 Test 3",
                    strcmp(rt_string_cstr(result), "125d7342b9ac11cd91a39af48aa17b4f63f175d3") ==
                        0);
    }

    printf("\n");
}

//=============================================================================
// HMAC-SHA256 Tests (RFC 4231)
//=============================================================================

static void test_hmac_sha256() {
    printf("Testing Hash.HmacSHA256:\n");

    // Test 1: key = 0x0b repeated 20 times, data = "Hi There"
    // Expected: b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7
    {
        uint8_t key_data[20];
        memset(key_data, 0x0b, 20);
        void *key = make_bytes(key_data, 20);
        void *data = make_bytes_str("Hi There");

        rt_string result = rt_hash_hmac_sha256_bytes(key, data);
        test_result("HMAC-SHA256 Test 1",
                    strcmp(rt_string_cstr(result),
                           "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7") ==
                        0);
    }

    // Test 2: key = "Jefe", data = "what do ya want for nothing?"
    // Expected: 5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843
    {
        rt_string key = rt_const_cstr("Jefe");
        rt_string data = rt_const_cstr("what do ya want for nothing?");

        rt_string result = rt_hash_hmac_sha256(key, data);
        test_result("HMAC-SHA256 Test 2",
                    strcmp(rt_string_cstr(result),
                           "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843") ==
                        0);
    }

    // Test 3: key = 0xaa repeated 20 times, data = 0xdd repeated 50 times
    // Expected: 773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe
    {
        uint8_t key_data[20];
        memset(key_data, 0xaa, 20);
        uint8_t data_bytes[50];
        memset(data_bytes, 0xdd, 50);

        void *key = make_bytes(key_data, 20);
        void *data = make_bytes(data_bytes, 50);

        rt_string result = rt_hash_hmac_sha256_bytes(key, data);
        test_result("HMAC-SHA256 Test 3",
                    strcmp(rt_string_cstr(result),
                           "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe") ==
                        0);
    }

    // Test 4: Long key (longer than block size - gets hashed)
    // key = 0xaa repeated 131 times
    // data = "Test Using Larger Than Block-Size Key - Hash Key First"
    // Expected: 60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54
    {
        uint8_t key_data[131];
        memset(key_data, 0xaa, 131);
        void *key = make_bytes(key_data, 131);
        void *data = make_bytes_str("Test Using Larger Than Block-Size Key - Hash Key First");

        rt_string result = rt_hash_hmac_sha256_bytes(key, data);
        test_result("HMAC-SHA256 Test 4 (long key)",
                    strcmp(rt_string_cstr(result),
                           "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54") ==
                        0);
    }

    printf("\n");
}

static void test_sha256_incremental_matches_one_shot() {
    printf("Testing SHA-256 incremental API:\n");

    static const char payload[] =
        "The quick brown fox jumps over the lazy dog while the TLS transcript grows.";
    uint8_t one_shot[32];
    uint8_t incremental[32];

    rt_sha256(payload, sizeof(payload) - 1, one_shot);

    rt_sha256_ctx ctx;
    rt_sha256_init(&ctx);
    rt_sha256_update(&ctx, payload, 13);
    rt_sha256_update(&ctx, payload + 13, 21);
    rt_sha256_update(&ctx, payload + 34, sizeof(payload) - 1 - 34);
    rt_sha256_final(&ctx, incremental);

    test_result("SHA-256 incremental matches one-shot", memcmp(one_shot, incremental, 32) == 0);
    printf("\n");
}

static void test_string_inputs_preserve_embedded_nul() {
    printf("Testing string APIs preserve embedded NUL bytes:\n");

    uint8_t msg_data[] = {'a', 0, 'b'};
    uint8_t key_data[] = {'k', 0, 'y'};
    rt_string msg = make_string_raw(msg_data, sizeof(msg_data));
    rt_string key = make_string_raw(key_data, sizeof(key_data));
    void *msg_bytes = make_bytes(msg_data, sizeof(msg_data));
    void *key_bytes = make_bytes(key_data, sizeof(key_data));

    test_result("MD5 string matches Bytes",
                strcmp(rt_string_cstr(rt_hash_md5(msg)), rt_string_cstr(rt_hash_md5_bytes(msg_bytes))) == 0);
    test_result("SHA1 string matches Bytes",
                strcmp(rt_string_cstr(rt_hash_sha1(msg)), rt_string_cstr(rt_hash_sha1_bytes(msg_bytes))) == 0);
    test_result("SHA256 string matches Bytes",
                strcmp(rt_string_cstr(rt_hash_sha256(msg)), rt_string_cstr(rt_hash_sha256_bytes(msg_bytes))) == 0);
    test_result("CRC32 string matches Bytes", rt_hash_crc32(msg) == rt_hash_crc32_bytes(msg_bytes));
    test_result("Fast hash string matches Bytes", rt_hash_fast(msg) == rt_hash_fast_bytes(msg_bytes));

    test_result("HMAC-MD5 string matches Bytes",
                strcmp(rt_string_cstr(rt_hash_hmac_md5(key, msg)),
                       rt_string_cstr(rt_hash_hmac_md5_bytes(key_bytes, msg_bytes))) == 0);
    test_result("HMAC-SHA1 string matches Bytes",
                strcmp(rt_string_cstr(rt_hash_hmac_sha1(key, msg)),
                       rt_string_cstr(rt_hash_hmac_sha1_bytes(key_bytes, msg_bytes))) == 0);
    test_result("HMAC-SHA256 string matches Bytes",
                strcmp(rt_string_cstr(rt_hash_hmac_sha256(key, msg)),
                       rt_string_cstr(rt_hash_hmac_sha256_bytes(key_bytes, msg_bytes))) == 0);

    printf("\n");
}

//=============================================================================
// PBKDF2-SHA256 Tests (RFC 6070 extended)
//=============================================================================

static void test_pbkdf2_sha256() {
    printf("Testing KeyDerive.Pbkdf2SHA256:\n");

    // Test 1: password="password", salt="salt", iterations=1000, dkLen=32
    // Note: RFC 6070 uses SHA1, but we're using SHA256
    // Expected: 632c2812e46d4604102ba7618e9d6d7d2f8128f6266b4a03264d2a0460b7dcb3
    {
        rt_string password = rt_const_cstr("password");
        void *salt = make_bytes_str("salt");

        rt_string result = rt_keyderive_pbkdf2_sha256_str(password, salt, 1000, 32);
        test_result("PBKDF2-SHA256 password/salt/1000/32",
                    strcmp(rt_string_cstr(result),
                           "632c2812e46d4604102ba7618e9d6d7d2f8128f6266b4a03264d2a0460b7dcb3") ==
                        0);
    }

    // Test 2: password="passwordPASSWORDpassword",
    // salt="saltSALTsaltSALTsaltSALTsaltSALTsalt", iterations=4096, dkLen=40
    // Expected: 348c89dbcbd32b2f32d814b8116e84cf2b17347ebc1800181c4e2a1fb8dd53e1c635518c7dac47e9
    {
        rt_string password = rt_const_cstr("passwordPASSWORDpassword");
        void *salt = make_bytes_str("saltSALTsaltSALTsaltSALTsaltSALTsalt");

        rt_string result = rt_keyderive_pbkdf2_sha256_str(password, salt, 4096, 40);
        test_result("PBKDF2-SHA256 long password/salt/4096/40",
                    strcmp(rt_string_cstr(result),
                           "348c89dbcbd32b2f32d814b8116e84cf2b17347ebc1800181c4e2a1fb8dd53e1c635518"
                           "c7dac47e9") == 0);
    }

    // Test 3: Returns Bytes object
    {
        rt_string password = rt_const_cstr("test");
        void *salt = make_bytes_str("salt");

        void *result = rt_keyderive_pbkdf2_sha256(password, salt, 1000, 16);
        test_result("PBKDF2-SHA256 returns Bytes", result != nullptr);
        test_result("PBKDF2-SHA256 Bytes has correct length", rt_bytes_len(result) == 16);
    }

    // Test 4: Embedded NUL bytes in passwords are significant
    {
        uint8_t full_password[] = {'p', 'a', 's', 's', 0, 'w', 'o', 'r', 'd'};
        rt_string password_full = make_string_raw(full_password, sizeof(full_password));
        rt_string password_prefix = rt_const_cstr("pass");
        void *salt = make_bytes_str("salt");

        rt_string full =
            rt_keyderive_pbkdf2_sha256_str(password_full, salt, 1000, 16);
        rt_string prefix =
            rt_keyderive_pbkdf2_sha256_str(password_prefix, salt, 1000, 16);
        test_result("PBKDF2 treats embedded NUL as password data",
                    strcmp(rt_string_cstr(full), rt_string_cstr(prefix)) != 0);
    }

    // Test 5: scrypt-SHA256 matches RFC 7914 test vector
    {
        rt_string password = rt_const_cstr("password");
        void *salt = make_bytes_str("NaCl");

        rt_string result = rt_keyderive_scrypt_sha256_str(password, salt, 1024, 8, 16, 64);
        test_result("scrypt-SHA256 RFC 7914 vector",
                    strcmp(rt_string_cstr(result),
                           "fdbabe1c9d3472007856e7190d01e9fe7c6ad7cbc8237830e77376634b373162"
                           "2eaf30d92e22a3886ff109279d9830dac727afb94a83ee6d8360cbdfa2cc0"
                           "640") == 0);
    }

    printf("\n");
}

static void test_constant_time_equals_and_passwords() {
    printf("Testing constant-time compare and Password:\n");

    rt_string mac1 = rt_const_cstr("abcdef012345");
    rt_string mac2 = rt_const_cstr("abcdef012345");
    rt_string mac3 = rt_const_cstr("abcdef012346");
    test_result("Hash.ConstantTimeEquals accepts equal strings",
                rt_hash_constant_time_equals(mac1, mac2) == 1);
    test_result("Hash.ConstantTimeEquals rejects different strings",
                rt_hash_constant_time_equals(mac1, mac3) == 0);

    void *b1 = make_bytes_str("tag");
    void *b2 = make_bytes_str("tag");
    void *b3 = make_bytes_str("tags");
    test_result("Hash.ConstantTimeEqualsBytes accepts equal bytes",
                rt_hash_constant_time_equals_bytes(b1, b2) == 1);
    test_result("Hash.ConstantTimeEqualsBytes rejects length mismatch",
                rt_hash_constant_time_equals_bytes(b1, b3) == 0);

    rt_string hash = rt_password_hash_scrypt_params(rt_const_cstr("secret"), 16, 1, 1);
    test_result("Password.HashScryptParams verifies",
                rt_password_verify(rt_const_cstr("secret"), hash) == 1);
    test_result("Password.Verify rejects wrong password",
                rt_password_verify(rt_const_cstr("wrong"), hash) == 0);
    test_result("Password.NeedsRehash flags weak scrypt params",
                rt_password_needs_rehash(hash) == 1);

    rt_string pbkdf2 = rt_password_hash_with_iterations(rt_const_cstr("secret"), 100000);
    test_result("Password.Verify accepts legacy PBKDF2",
                rt_password_verify(rt_const_cstr("secret"), pbkdf2) == 1);
    test_result("Password.NeedsRehash flags PBKDF2",
                rt_password_needs_rehash(pbkdf2) == 1);

    printf("\n");
}

static void test_high_level_aead_wrappers() {
    printf("Testing high-level AEAD wrappers:\n");

    const uint8_t plain_data[] = {'s', 'e', 'c', 'r', 'e', 't'};
    void *plain = make_bytes(plain_data, sizeof(plain_data));
    void *aad = make_bytes_str("context");
    void *wrong_aad = make_bytes_str("other");

    void *cipher = rt_cipher_encrypt_aad(plain, rt_const_cstr("password"), aad);
    test_result("Cipher.EncryptAAD uses versioned format",
                cipher && rt_bytes_len(cipher) > 4 && rt_bytes_get(cipher, 0) == 'V' &&
                    rt_bytes_get(cipher, 1) == 'C' && rt_bytes_get(cipher, 2) == 'P' &&
                    rt_bytes_get(cipher, 3) == '2');
    void *opened = rt_cipher_decrypt_aad(cipher, rt_const_cstr("password"), aad);
    test_result("Cipher.DecryptAAD round-trips",
                bytes_equal(opened, plain_data, sizeof(plain_data)));
    test_result("Cipher.DecryptAAD rejects wrong AAD",
                rt_cipher_decrypt_aad(cipher, rt_const_cstr("password"), wrong_aad) == nullptr);

    void *key = rt_cipher_generate_key();
    void *key_cipher = rt_cipher_encrypt_with_key_aad(plain, key, aad);
    test_result("Cipher.EncryptWithKeyAAD uses versioned format",
                key_cipher && rt_bytes_len(key_cipher) > 4 && rt_bytes_get(key_cipher, 0) == 'V' &&
                    rt_bytes_get(key_cipher, 1) == 'C' && rt_bytes_get(key_cipher, 2) == 'K' &&
                    rt_bytes_get(key_cipher, 3) == '2');
    void *key_opened = rt_cipher_decrypt_with_key_aad(key_cipher, key, aad);
    test_result("Cipher.DecryptWithKeyAAD round-trips",
                bytes_equal(key_opened, plain_data, sizeof(plain_data)));
    test_result("Cipher.DecryptWithKeyAAD rejects wrong AAD",
                rt_cipher_decrypt_with_key_aad(key_cipher, key, wrong_aad) == nullptr);

    uint8_t aes_key_data[16];
    memset(aes_key_data, 0x42, sizeof(aes_key_data));
    void *aes_key = make_bytes(aes_key_data, sizeof(aes_key_data));
    void *aes_cipher = rt_aes_encrypt_auth(plain, aes_key, aad);
    test_result("Aes.EncryptAuth uses versioned format",
                aes_cipher && rt_bytes_len(aes_cipher) > 4 && rt_bytes_get(aes_cipher, 0) == 'V' &&
                    rt_bytes_get(aes_cipher, 1) == 'A' && rt_bytes_get(aes_cipher, 2) == 'K' &&
                    rt_bytes_get(aes_cipher, 3) == '1');
    void *aes_opened = rt_aes_decrypt_auth(aes_cipher, aes_key, aad);
    test_result("Aes.DecryptAuth round-trips",
                bytes_equal(aes_opened, plain_data, sizeof(plain_data)));
    test_result("Aes.DecryptAuth rejects wrong AAD",
                rt_aes_decrypt_auth(aes_cipher, aes_key, wrong_aad) == nullptr);

    printf("\n");
}

//=============================================================================
// Secure Random Tests
//=============================================================================

static void test_crypto_rand() {
    printf("Testing Rand:\n");

    // Test 1: Bytes returns correct length
    {
        void *bytes = rt_crypto_rand_bytes(32);
        test_result("Rand.Bytes returns correct length", rt_bytes_len(bytes) == 32);
    }

    // Test 1b: Bytes(0) returns an empty Bytes object
    {
        void *bytes = rt_crypto_rand_bytes(0);
        test_result("Rand.Bytes(0) returns empty Bytes", rt_bytes_len(bytes) == 0);
    }

    // Test 2: Multiple calls produce different results
    {
        void *bytes1 = rt_crypto_rand_bytes(16);
        void *bytes2 = rt_crypto_rand_bytes(16);

        bool different = false;
        for (int i = 0; i < 16; i++) {
            if (rt_bytes_get(bytes1, i) != rt_bytes_get(bytes2, i)) {
                different = true;
                break;
            }
        }
        test_result("Rand.Bytes produces different results", different);
    }

    // Test 3: Int returns values in range
    {
        bool all_in_range = true;
        for (int i = 0; i < 100; i++) {
            int64_t val = rt_crypto_rand_int(10, 20);
            if (val < 10 || val > 20) {
                all_in_range = false;
                break;
            }
        }
        test_result("Rand.Int returns values in range [10, 20]", all_in_range);
    }

    // Test 4: Int with min == max returns that value
    {
        int64_t val = rt_crypto_rand_int(42, 42);
        test_result("Rand.Int with min==max returns that value", val == 42);
    }

    // Test 5: Int produces variety (not always same value)
    {
        std::set<int64_t> values;
        for (int i = 0; i < 50; i++) {
            values.insert(rt_crypto_rand_int(0, 100));
        }
        // Should have at least 10 different values in 50 tries
        test_result("Rand.Int produces variety", values.size() >= 10);
    }

    // Test 6: Int with negative range
    {
        bool all_in_range = true;
        for (int i = 0; i < 100; i++) {
            int64_t val = rt_crypto_rand_int(-100, -50);
            if (val < -100 || val > -50) {
                all_in_range = false;
                break;
            }
        }
        test_result("Rand.Int with negative range", all_in_range);
    }

    // Test 7: Int with range spanning zero
    {
        bool saw_negative = false;
        bool saw_positive = false;
        for (int i = 0; i < 100; i++) {
            int64_t val = rt_crypto_rand_int(-10, 10);
            if (val < 0)
                saw_negative = true;
            if (val > 0)
                saw_positive = true;
        }
        test_result("Rand.Int spanning zero produces negatives and positives",
                    saw_negative && saw_positive);
    }

    // Test 8: Int handles ranges that used to overflow signed arithmetic
    {
        bool ok = true;
        for (int i = 0; i < 20; i++) {
            int64_t neg = rt_crypto_rand_int(std::numeric_limits<int64_t>::min(), -1);
            int64_t pos = rt_crypto_rand_int(0, std::numeric_limits<int64_t>::max());
            (void)rt_crypto_rand_int(std::numeric_limits<int64_t>::min(),
                                     std::numeric_limits<int64_t>::max());
            if (neg > -1 || pos < 0) {
                ok = false;
                break;
            }
        }
        test_result("Rand.Int handles extreme signed ranges", ok);
    }

    printf("\n");
}

//=============================================================================
// AEAD / Key Exchange Tests
//=============================================================================

static void test_aead_tamper_detection() {
    printf("Testing AEAD tamper detection:\n");

    {
        uint8_t key[32];
        uint8_t nonce[12];
        memset(key, 0x11, sizeof(key));
        memset(nonce, 0x22, sizeof(nonce));
        const char *msg = "ChaCha20-Poly1305";
        uint8_t ciphertext[sizeof("ChaCha20-Poly1305") - 1 + 16];
        uint8_t plaintext[sizeof("ChaCha20-Poly1305") - 1];

        size_t cipher_len =
            rt_chacha20_poly1305_encrypt(key, nonce, nullptr, 0, msg, strlen(msg), ciphertext);
        test_result("ChaCha20 encrypt length matches", cipher_len == strlen(msg) + 16);
        ciphertext[cipher_len - 1] ^= 0x01;
        test_result("ChaCha20 tamper detected",
                    rt_chacha20_poly1305_decrypt(
                        key, nonce, nullptr, 0, ciphertext, cipher_len, plaintext) < 0);
        test_result("ChaCha20 rejects NULL AAD with nonzero length",
                    rt_chacha20_poly1305_encrypt(
                        key, nonce, nullptr, 1, msg, strlen(msg), ciphertext) == 0);
    }

    {
        uint8_t key[16];
        uint8_t nonce[12];
        memset(key, 0x33, sizeof(key));
        memset(nonce, 0x44, sizeof(nonce));
        const char *msg = "AES-GCM";
        uint8_t ciphertext[sizeof("AES-GCM") - 1 + 16];
        uint8_t plaintext[sizeof("AES-GCM") - 1];

        size_t cipher_len =
            rt_aes128_gcm_encrypt(key, nonce, nullptr, 0, msg, strlen(msg), ciphertext);
        test_result("AES-GCM encrypt length matches", cipher_len == strlen(msg) + 16);
        ciphertext[0] ^= 0x80;
        test_result(
            "AES-GCM tamper detected",
            rt_aes128_gcm_decrypt(key, nonce, nullptr, 0, ciphertext, cipher_len, plaintext) < 0);
        test_result("AES-GCM rejects NULL AAD with nonzero length",
                    rt_aes128_gcm_encrypt(key, nonce, nullptr, 1, msg, strlen(msg), ciphertext) ==
                        0);
    }

    printf("\n");
}

static void test_x25519_shared_secret_agreement() {
    printf("Testing X25519 shared secret agreement:\n");

    uint8_t alice_secret[32], alice_public[32];
    uint8_t bob_secret[32], bob_public[32];
    uint8_t shared1[32], shared2[32];

    rt_x25519_keygen(alice_secret, alice_public);
    rt_x25519_keygen(bob_secret, bob_public);
    test_result("Alice X25519 succeeds", rt_x25519(alice_secret, bob_public, shared1) == 0);
    test_result("Bob X25519 succeeds", rt_x25519(bob_secret, alice_public, shared2) == 0);

    test_result("Shared secrets match", memcmp(shared1, shared2, sizeof(shared1)) == 0);

    uint8_t zeros[32] = {0};
    test_result("Shared secret is non-zero", memcmp(shared1, zeros, sizeof(shared1)) != 0);
    test_result("All-zero peer public key is rejected",
                rt_x25519(alice_secret, zeros, shared1) != 0);
    printf("\n");
}

//=============================================================================
// Entry Point
//=============================================================================

int main() {
    printf("=== RT Crypto Tests ===\n\n");

    test_hmac_md5();
    test_hmac_sha1();
    test_hmac_sha256();
    test_sha256_incremental_matches_one_shot();
    test_string_inputs_preserve_embedded_nul();
    test_pbkdf2_sha256();
    test_constant_time_equals_and_passwords();
    test_high_level_aead_wrappers();
    test_crypto_rand();
    test_aead_tamper_detection();
    test_x25519_shared_secret_agreement();

    printf("All Crypto tests passed!\n");
    return 0;
}
