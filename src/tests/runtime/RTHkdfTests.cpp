//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTHkdfTests.cpp
// Purpose: Validate HKDF-SHA256 (RFC 5869) extract/expand produce correct
//          output and that secure zeroing does not corrupt results.
// Key invariants:
//   - HKDF-Extract and HKDF-Expand match RFC 5869 test vectors.
//   - Repeated invocations produce identical results (no state leakage).
// Links: src/runtime/network/rt_crypto.c
//
//===----------------------------------------------------------------------===//

#include "rt_crypto.h"

#include <cassert>
#include <cstdio>
#include <cstring>

/// @brief Helper to print test result.
static void test_result(const char *name, bool passed) {
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
    assert(passed);
}

/// @brief Convert a hex string to bytes.
static void hex_to_bytes(const char *hex, uint8_t *out, size_t len) {
    for (size_t i = 0; i < len; i++) {
        unsigned int b;
        sscanf(hex + 2 * i, "%02x", &b);
        out[i] = (uint8_t)b;
    }
}

//=============================================================================
// HKDF-Extract Tests (RFC 5869 Test Case 1)
//=============================================================================

static void test_hkdf_extract_rfc5869_case1() {
    printf("Testing HKDF-Extract (RFC 5869 Case 1):\n");

    // IKM  = 0x0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b (22 octets)
    // salt = 0x000102030405060708090a0b0c (13 octets)
    // PRK  = 0x077709362c2e32df0ddc3f0dc47bba6390b6c73bb50f9c3122ec844ad7c2b3e5 (32 octets)
    uint8_t ikm[22];
    memset(ikm, 0x0b, 22);

    uint8_t salt[13];
    for (int i = 0; i < 13; i++)
        salt[i] = (uint8_t)i;

    uint8_t expected_prk[32];
    hex_to_bytes(
        "077709362c2e32df0ddc3f0dc47bba6390b6c73bb50f9c3122ec844ad7c2b3e5", expected_prk, 32);

    uint8_t prk[32];
    rt_hkdf_extract(salt, 13, ikm, 22, prk);

    test_result("Extract matches RFC 5869 Case 1", memcmp(prk, expected_prk, 32) == 0);
    printf("\n");
}

//=============================================================================
// HKDF-Expand Tests (RFC 5869 Test Case 1)
//=============================================================================

static void test_hkdf_expand_rfc5869_case1() {
    printf("Testing HKDF-Expand (RFC 5869 Case 1):\n");

    // PRK from extract step above
    uint8_t prk[32];
    hex_to_bytes("077709362c2e32df0ddc3f0dc47bba6390b6c73bb50f9c3122ec844ad7c2b3e5", prk, 32);

    // info = 0xf0f1f2f3f4f5f6f7f8f9 (10 octets)
    uint8_t info[10];
    for (int i = 0; i < 10; i++)
        info[i] = (uint8_t)(0xf0 + i);

    // OKM  = 0x3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf
    //         34007208d5b887185865 (42 octets)
    uint8_t expected_okm[42];
    hex_to_bytes("3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf"
                 "34007208d5b887185865",
                 expected_okm,
                 42);

    uint8_t okm[42];
    rt_hkdf_expand(prk, info, 10, okm, 42);

    test_result("Expand matches RFC 5869 Case 1", memcmp(okm, expected_okm, 42) == 0);
    printf("\n");
}

//=============================================================================
// HKDF-Extract with NULL salt (uses zero salt)
//=============================================================================

static void test_hkdf_extract_null_salt() {
    printf("Testing HKDF-Extract with NULL salt:\n");

    uint8_t ikm[22];
    memset(ikm, 0x0b, 22);

    // With NULL salt, should use a zero-filled salt of hash length
    uint8_t prk1[32], prk2[32];
    rt_hkdf_extract(NULL, 0, ikm, 22, prk1);
    rt_hkdf_extract(NULL, 0, ikm, 22, prk2);

    // Same input should give same output (no state corruption from secure zero)
    test_result("NULL salt: repeatable", memcmp(prk1, prk2, 32) == 0);

    // The output should not be all zeros (i.e., something was actually computed)
    uint8_t zeros[32] = {0};
    test_result("NULL salt: non-trivial output", memcmp(prk1, zeros, 32) != 0);

    printf("\n");
}

//=============================================================================
// HKDF repeatability (proves secure_zero doesn't corrupt)
//=============================================================================

static void test_hkdf_repeatability() {
    printf("Testing HKDF repeatability (secure_zero safety):\n");

    uint8_t ikm[32];
    for (int i = 0; i < 32; i++)
        ikm[i] = (uint8_t)(i * 7 + 3);

    uint8_t salt[16];
    for (int i = 0; i < 16; i++)
        salt[i] = (uint8_t)(i + 0x10);

    uint8_t info[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

    // Run HKDF extract + expand multiple times
    uint8_t results[4][64];
    for (int trial = 0; trial < 4; trial++) {
        uint8_t prk[32];
        rt_hkdf_extract(salt, 16, ikm, 32, prk);
        rt_hkdf_expand(prk, info, 8, results[trial], 64);
    }

    // All results should be identical
    bool all_match = true;
    for (int trial = 1; trial < 4; trial++) {
        if (memcmp(results[0], results[trial], 64) != 0) {
            all_match = false;
            break;
        }
    }
    test_result("4 trials identical", all_match);

    // Output should not be trivial
    uint8_t zeros[64] = {0};
    test_result("Non-trivial output", memcmp(results[0], zeros, 64) != 0);

    printf("\n");
}

//=============================================================================
// HMAC-SHA256 correctness (ensures secure_zero in rt_hmac_sha256 is safe)
//=============================================================================

static void test_hmac_sha256_after_secure_zero() {
    printf("Testing HMAC-SHA256 correctness (post secure_zero):\n");

    // RFC 4231 Test Case 2: key = "Jefe", data = "what do ya want for nothing?"
    const char *key_str = "Jefe";
    const char *data_str = "what do ya want for nothing?";

    uint8_t expected[32];
    hex_to_bytes("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843", expected, 32);

    // Run it twice to ensure the first call's secure_zero doesn't affect the second
    uint8_t mac1[32], mac2[32];
    rt_hmac_sha256((const uint8_t *)key_str, strlen(key_str), data_str, strlen(data_str), mac1);
    rt_hmac_sha256((const uint8_t *)key_str, strlen(key_str), data_str, strlen(data_str), mac2);

    test_result("HMAC-SHA256 matches RFC 4231 (call 1)", memcmp(mac1, expected, 32) == 0);
    test_result("HMAC-SHA256 matches RFC 4231 (call 2)", memcmp(mac2, expected, 32) == 0);

    printf("\n");
}

//=============================================================================
// Entry Point
//=============================================================================

int main() {
    printf("=== HKDF / Secure-Zero Tests ===\n\n");

    test_hkdf_extract_rfc5869_case1();
    test_hkdf_expand_rfc5869_case1();
    test_hkdf_extract_null_salt();
    test_hkdf_repeatability();
    test_hmac_sha256_after_secure_zero();

    printf("All HKDF tests passed!\n");
    return 0;
}
