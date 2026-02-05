//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTCipherTests.cpp
// Purpose: Validate high-level encryption/decryption API.
// Key invariants: Encrypt-decrypt round-trips produce original data.
// Links: docs/viperlib/crypto.md
//
//===----------------------------------------------------------------------===//

#include "rt_bytes.h"
#include "rt_cipher.h"
#include "rt_string.h"

#include <cassert>
#include <cstdio>
#include <cstring>

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

/// @brief Compare two Bytes objects for equality.
static bool bytes_equal(void *a, void *b)
{
    int64_t len_a = rt_bytes_len(a);
    int64_t len_b = rt_bytes_len(b);
    if (len_a != len_b)
        return false;

    for (int64_t i = 0; i < len_a; i++)
    {
        if (rt_bytes_get(a, i) != rt_bytes_get(b, i))
            return false;
    }
    return true;
}

//=============================================================================
// Password-Based Encryption Tests
//=============================================================================

static void test_password_encrypt_decrypt_roundtrip()
{
    printf("Testing Cipher password-based encrypt/decrypt:\n");

    // Test 1: Basic roundtrip
    {
        const char *plaintext = "Hello, World!";
        void *plain = make_bytes_str(plaintext);
        rt_string password = rt_const_cstr("my-secret-password");

        void *encrypted = rt_cipher_encrypt(plain, password);
        test_result("Encrypt produces output", encrypted != NULL);
        test_result("Encrypted is larger than plaintext",
                    rt_bytes_len(encrypted) > rt_bytes_len(plain));

        void *decrypted = rt_cipher_decrypt(encrypted, password);
        test_result("Decrypt produces output", decrypted != NULL);
        test_result("Decrypted matches original", bytes_equal(plain, decrypted));
    }

    // Test 2: Empty plaintext
    {
        void *plain = rt_bytes_new(0);
        rt_string password = rt_const_cstr("password");

        void *encrypted = rt_cipher_encrypt(plain, password);
        test_result("Empty plaintext encrypts", encrypted != NULL);
        // Expected: 16 (salt) + 12 (nonce) + 0 (ciphertext) + 16 (tag) = 44 bytes
        test_result("Empty encrypted has correct size", rt_bytes_len(encrypted) == 44);

        void *decrypted = rt_cipher_decrypt(encrypted, password);
        test_result("Empty decrypts correctly", rt_bytes_len(decrypted) == 0);
    }

    // Test 3: Large data
    {
        const size_t size = 10000;
        void *plain = rt_bytes_new((int64_t)size);
        for (size_t i = 0; i < size; i++)
        {
            rt_bytes_set(plain, (int64_t)i, (int64_t)(i % 256));
        }
        rt_string password = rt_const_cstr("large-data-password");

        void *encrypted = rt_cipher_encrypt(plain, password);
        void *decrypted = rt_cipher_decrypt(encrypted, password);
        test_result("Large data roundtrip", bytes_equal(plain, decrypted));
    }

    // Test 4: Different passwords produce different ciphertext
    {
        void *plain = make_bytes_str("Same plaintext");
        rt_string pw1 = rt_const_cstr("password1");
        rt_string pw2 = rt_const_cstr("password2");

        void *enc1 = rt_cipher_encrypt(plain, pw1);
        void *enc2 = rt_cipher_encrypt(plain, pw2);

        // Due to random salt and nonce, even same password produces different output
        test_result("Different outputs (randomness)", !bytes_equal(enc1, enc2));
    }

    printf("\n");
}

//=============================================================================
// Key-Based Encryption Tests
//=============================================================================

static void test_key_based_encrypt_decrypt()
{
    printf("Testing Cipher key-based encrypt/decrypt:\n");

    // Test 1: Generate key and roundtrip
    {
        void *key = rt_cipher_generate_key();
        test_result("GenerateKey produces 32 bytes", rt_bytes_len(key) == 32);

        void *plain = make_bytes_str("Secret message with key");
        void *encrypted = rt_cipher_encrypt_with_key(plain, key);
        test_result("EncryptWithKey produces output", encrypted != NULL);

        void *decrypted = rt_cipher_decrypt_with_key(encrypted, key);
        test_result("DecryptWithKey roundtrip", bytes_equal(plain, decrypted));
    }

    // Test 2: Key-based encryption is smaller (no salt)
    {
        void *key = rt_cipher_generate_key();
        void *plain = make_bytes_str("Test");
        rt_string password = rt_const_cstr("password");

        void *enc_pw = rt_cipher_encrypt(plain, password);
        void *enc_key = rt_cipher_encrypt_with_key(plain, key);

        // Password-based: salt(16) + nonce(12) + cipher + tag(16)
        // Key-based: nonce(12) + cipher + tag(16)
        // Difference should be exactly 16 bytes (salt size)
        test_result("Key-based is 16 bytes smaller",
                    rt_bytes_len(enc_pw) - rt_bytes_len(enc_key) == 16);
    }

    printf("\n");
}

//=============================================================================
// Key Derivation Tests
//=============================================================================

static void test_key_derivation()
{
    printf("Testing Cipher key derivation:\n");

    // Test 1: DeriveKey produces consistent keys
    {
        rt_string password = rt_const_cstr("test-password");
        void *salt = make_bytes_str("fixed-salt-1234!");

        void *key1 = rt_cipher_derive_key(password, salt);
        void *key2 = rt_cipher_derive_key(password, salt);

        test_result("DeriveKey produces 32 bytes", rt_bytes_len(key1) == 32);
        test_result("Same inputs produce same key", bytes_equal(key1, key2));
    }

    // Test 2: Different salts produce different keys
    {
        rt_string password = rt_const_cstr("test-password");
        void *salt1 = make_bytes_str("salt-one-here!");
        void *salt2 = make_bytes_str("salt-two-here!");

        void *key1 = rt_cipher_derive_key(password, salt1);
        void *key2 = rt_cipher_derive_key(password, salt2);

        test_result("Different salts produce different keys", !bytes_equal(key1, key2));
    }

    // Test 3: Different passwords produce different keys
    {
        void *salt = make_bytes_str("common-salt!!!!!");
        rt_string pw1 = rt_const_cstr("password-one");
        rt_string pw2 = rt_const_cstr("password-two");

        void *key1 = rt_cipher_derive_key(pw1, salt);
        void *key2 = rt_cipher_derive_key(pw2, salt);

        test_result("Different passwords produce different keys", !bytes_equal(key1, key2));
    }

    // Test 4: Derived key works with key-based encryption
    {
        rt_string password = rt_const_cstr("my-password");
        void *salt = make_bytes_str("my-salt-value!!!");
        void *key = rt_cipher_derive_key(password, salt);

        void *plain = make_bytes_str("Message encrypted with derived key");
        void *encrypted = rt_cipher_encrypt_with_key(plain, key);
        void *decrypted = rt_cipher_decrypt_with_key(encrypted, key);

        test_result("Derived key roundtrip", bytes_equal(plain, decrypted));
    }

    printf("\n");
}

//=============================================================================
// Randomness Tests
//=============================================================================

static void test_encryption_randomness()
{
    printf("Testing Cipher encryption randomness:\n");

    // Same plaintext and password should produce different ciphertext each time
    // (due to random salt and nonce)
    {
        void *plain = make_bytes_str("Same plaintext every time");
        rt_string password = rt_const_cstr("same-password");

        void *enc1 = rt_cipher_encrypt(plain, password);
        void *enc2 = rt_cipher_encrypt(plain, password);
        void *enc3 = rt_cipher_encrypt(plain, password);

        test_result("Randomness: enc1 != enc2", !bytes_equal(enc1, enc2));
        test_result("Randomness: enc2 != enc3", !bytes_equal(enc2, enc3));
        test_result("Randomness: enc1 != enc3", !bytes_equal(enc1, enc3));

        // But all should decrypt to same plaintext
        void *dec1 = rt_cipher_decrypt(enc1, password);
        void *dec2 = rt_cipher_decrypt(enc2, password);
        void *dec3 = rt_cipher_decrypt(enc3, password);

        test_result("All decrypt to same", bytes_equal(dec1, dec2) && bytes_equal(dec2, dec3));
        test_result("Decrypted matches original", bytes_equal(plain, dec1));
    }

    printf("\n");
}

//=============================================================================
// Edge Cases
//=============================================================================

static void test_edge_cases()
{
    printf("Testing Cipher edge cases:\n");

    // Test binary data (all byte values)
    {
        void *plain = rt_bytes_new(256);
        for (int i = 0; i < 256; i++)
        {
            rt_bytes_set(plain, i, i);
        }
        rt_string password = rt_const_cstr("binary-test");

        void *encrypted = rt_cipher_encrypt(plain, password);
        void *decrypted = rt_cipher_decrypt(encrypted, password);

        test_result("Binary data roundtrip", bytes_equal(plain, decrypted));
    }

    // Test with null bytes in plaintext
    {
        uint8_t data[] = {'H', 'e', 0, 'l', 0, 'o', 0};
        void *plain = make_bytes(data, sizeof(data));
        rt_string password = rt_const_cstr("null-bytes-test");

        void *encrypted = rt_cipher_encrypt(plain, password);
        void *decrypted = rt_cipher_decrypt(encrypted, password);

        test_result("Null bytes in plaintext roundtrip", bytes_equal(plain, decrypted));
    }

    printf("\n");
}

//=============================================================================
// Entry Point
//=============================================================================

int main()
{
    printf("=== RT Cipher Tests ===\n\n");

    test_password_encrypt_decrypt_roundtrip();
    test_key_based_encrypt_decrypt();
    test_key_derivation();
    test_encryption_randomness();
    test_edge_cases();

    printf("All Cipher tests passed!\n");
    return 0;
}
