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
#include "rt_hash.h"
#include "rt_keyderive.h"
#include "rt_rand.h"
#include "rt_string.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <set>

/// @brief Helper to print test result.
static void test_result(const char *name, bool passed)
{
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
    assert(passed);
}

/// @brief Create a Bytes object from raw data.
static void *make_bytes(const uint8_t *data, size_t len)
{
    void *bytes = rt_bytes_new((int64_t)len);
    for (size_t i = 0; i < len; i++)
    {
        rt_bytes_set(bytes, (int64_t)i, data[i]);
    }
    return bytes;
}

/// @brief Create a Bytes object from a C string.
static void *make_bytes_str(const char *str)
{
    return make_bytes((const uint8_t *)str, strlen(str));
}

//=============================================================================
// HMAC-MD5 Tests (RFC 2202)
//=============================================================================

static void test_hmac_md5()
{
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

static void test_hmac_sha1()
{
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

static void test_hmac_sha256()
{
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

//=============================================================================
// PBKDF2-SHA256 Tests (RFC 6070 extended)
//=============================================================================

static void test_pbkdf2_sha256()
{
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

    printf("\n");
}

//=============================================================================
// Secure Random Tests
//=============================================================================

static void test_crypto_rand()
{
    printf("Testing Rand:\n");

    // Test 1: Bytes returns correct length
    {
        void *bytes = rt_crypto_rand_bytes(32);
        test_result("Rand.Bytes returns correct length", rt_bytes_len(bytes) == 32);
    }

    // Test 2: Multiple calls produce different results
    {
        void *bytes1 = rt_crypto_rand_bytes(16);
        void *bytes2 = rt_crypto_rand_bytes(16);

        bool different = false;
        for (int i = 0; i < 16; i++)
        {
            if (rt_bytes_get(bytes1, i) != rt_bytes_get(bytes2, i))
            {
                different = true;
                break;
            }
        }
        test_result("Rand.Bytes produces different results", different);
    }

    // Test 3: Int returns values in range
    {
        bool all_in_range = true;
        for (int i = 0; i < 100; i++)
        {
            int64_t val = rt_crypto_rand_int(10, 20);
            if (val < 10 || val > 20)
            {
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
        for (int i = 0; i < 50; i++)
        {
            values.insert(rt_crypto_rand_int(0, 100));
        }
        // Should have at least 10 different values in 50 tries
        test_result("Rand.Int produces variety", values.size() >= 10);
    }

    // Test 6: Int with negative range
    {
        bool all_in_range = true;
        for (int i = 0; i < 100; i++)
        {
            int64_t val = rt_crypto_rand_int(-100, -50);
            if (val < -100 || val > -50)
            {
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
        for (int i = 0; i < 100; i++)
        {
            int64_t val = rt_crypto_rand_int(-10, 10);
            if (val < 0)
                saw_negative = true;
            if (val > 0)
                saw_positive = true;
        }
        test_result("Rand.Int spanning zero produces negatives and positives",
                    saw_negative && saw_positive);
    }

    printf("\n");
}

//=============================================================================
// Entry Point
//=============================================================================

int main()
{
    printf("=== RT Crypto Tests ===\n\n");

    test_hmac_md5();
    test_hmac_sha1();
    test_hmac_sha256();
    test_pbkdf2_sha256();
    test_crypto_rand();

    printf("All Crypto tests passed!\n");
    return 0;
}
