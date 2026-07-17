//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTPasswordTests.cpp
// Purpose: Validate password hashing and verification functions.
// Key invariants: Hash produces valid format; Verify matches correctly.
//
//===----------------------------------------------------------------------===//

#include "rt_crypto_module.h"
#include "rt_password.h"
#include "rt_string.h"

#include <cassert>
#include <cstdio>
#include <cstring>

/// @brief Helper to print test result.
static void test_result(const char *name, bool passed) {
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
    assert(passed);
}

static rt_string make_string_raw(const char *data, size_t len) {
    return rt_string_from_bytes(data, len);
}

//=============================================================================
// Password Hash Format Tests
//=============================================================================

static void test_password_hash_format() {
    printf("Testing Password.Hash format:\n");

    // Test 1: Hash produces valid format "SCRYPT$log2N$r$p$salt_b64$hash_b64"
    {
        rt_string password = rt_const_cstr("mypassword123");
        rt_string hash = rt_password_hash(password);
        const char *hash_str = rt_string_cstr(hash);

        test_result("Hash starts with SCRYPT$", strncmp(hash_str, "SCRYPT$", 7) == 0);

        // Count $ delimiters (should be exactly 5)
        int count = 0;
        for (const char *p = hash_str; *p; p++) {
            if (*p == '$')
                count++;
        }
        test_result("Hash has 5 delimiters", count == 5);
        test_result("Default scrypt hash does not need rehash",
                    rt_password_needs_rehash(hash) == 0);
    }

    // Test 2: Legacy PBKDF2 hash with custom iterations
    {
        rt_string password = rt_const_cstr("testpass");
        rt_string hash = rt_password_hash_with_iterations(password, 350000);
        const char *hash_str = rt_string_cstr(hash);

        // Should contain the requested iteration count
        test_result("PBKDF2 HashIters starts with PBKDF2$", strncmp(hash_str, "PBKDF2$", 7) == 0);
        test_result("Hash contains custom iterations", strstr(hash_str, "$350000$") != nullptr);
        test_result("Legacy PBKDF2 hash needs rehash", rt_password_needs_rehash(hash) == 1);
    }

    // Test 3: STRONGER custom scrypt parameters are current, not stale
    // (VDOC-174): NeedsRehash uses a monotonic policy comparison, so a hash at
    // or above the policy minimum must not be reported as needing a rehash.
    {
        rt_string password = rt_const_cstr("testpass");
        rt_string hash = rt_password_hash_scrypt_params(password, 16384, 8, 2);
        const char *hash_str = rt_string_cstr(hash);

        test_result("HashScryptParams encodes parameters", strstr(hash_str, "$14$8$2$") != nullptr);
        test_result("Stronger p scrypt hash does NOT need rehash",
                    rt_password_needs_rehash(hash) == 0);

        // A larger N (32768 = 2^15) is also stronger than the default and current.
        rt_string strong_n = rt_password_hash_scrypt_params(password, 32768, 8, 1);
        test_result("HashScryptParams encodes a larger N",
                    strstr(rt_string_cstr(strong_n), "$15$8$1$") != nullptr);
        test_result("Larger-N scrypt hash does NOT need rehash",
                    rt_password_needs_rehash(strong_n) == 0);
    }

    // Test 4: Different passwords produce different hashes
    {
        rt_string pwd1 = rt_const_cstr("password1");
        rt_string pwd2 = rt_const_cstr("password2");
        rt_string hash1 = rt_password_hash(pwd1);
        rt_string hash2 = rt_password_hash(pwd2);

        test_result("Different passwords produce different hashes",
                    strcmp(rt_string_cstr(hash1), rt_string_cstr(hash2)) != 0);
    }

    // Test 5: Same password produces different hashes (due to random salt)
    {
        rt_string password = rt_const_cstr("samepassword");
        rt_string hash1 = rt_password_hash(password);
        rt_string hash2 = rt_password_hash(password);

        test_result("Same password produces different hashes (random salt)",
                    strcmp(rt_string_cstr(hash1), rt_string_cstr(hash2)) != 0);
    }

    printf("\n");
}

//=============================================================================
// Password Verification Tests
//=============================================================================

static void test_password_verify() {
    printf("Testing Password.Verify:\n");

    // Test 1: Correct password verifies
    {
        rt_string password = rt_const_cstr("correctpassword");
        rt_string hash = rt_password_hash(password);
        int8_t result = rt_password_verify(password, hash);
        test_result("Correct password verifies", result == 1);
    }

    // Test 2: Wrong password fails
    {
        rt_string password = rt_const_cstr("correctpassword");
        rt_string wrong = rt_const_cstr("wrongpassword");
        rt_string hash = rt_password_hash(password);
        int8_t result = rt_password_verify(wrong, hash);
        test_result("Wrong password fails", result == 0);
    }

    // Test 3: Empty password can be hashed and verified
    {
        rt_string password = rt_const_cstr("");
        rt_string hash = rt_password_hash(password);
        int8_t result = rt_password_verify(password, hash);
        test_result("Empty password verifies", result == 1);
    }

    // Test 4: Long password works
    {
        rt_string password =
            rt_const_cstr("This is a very long password that exceeds the normal length "
                          "that most people would use for their passwords, but it should "
                          "still work correctly with the password hashing algorithm.");
        rt_string hash = rt_password_hash(password);
        int8_t result = rt_password_verify(password, hash);
        test_result("Long password verifies", result == 1);
    }

    // Test 5: Unicode password works
    {
        rt_string password = rt_const_cstr("pässwörd123");
        rt_string hash = rt_password_hash(password);
        int8_t result = rt_password_verify(password, hash);
        test_result("Unicode password verifies", result == 1);
    }

    // Test 6: Verify with different iteration count (hash includes iterations)
    {
        rt_string password = rt_const_cstr("testpassword");
        rt_string hash = rt_password_hash_with_iterations(password, 250000);
        int8_t result = rt_password_verify(password, hash);
        test_result("Custom iteration hash verifies", result == 1);
    }

    // Test 7: Embedded NUL bytes in passwords are significant
    {
        const char full_raw[] = {'p', 'a', 's', 's', 0, 'w', 'o', 'r', 'd'};
        rt_string full_password = make_string_raw(full_raw, sizeof(full_raw));
        rt_string prefix_password = rt_const_cstr("pass");
        rt_string hash = rt_password_hash_with_iterations(full_password, 100000);
        test_result("Embedded NUL password verifies", rt_password_verify(full_password, hash) == 1);
        test_result("Embedded NUL password differs from prefix",
                    rt_password_verify(prefix_password, hash) == 0);
    }

    printf("\n");
}

//=============================================================================
// Invalid Input Tests
//=============================================================================

static void test_password_invalid_input() {
    printf("Testing Password invalid inputs:\n");

    // Test 1: Invalid hash format returns 0
    {
        rt_string password = rt_const_cstr("password");
        rt_string invalid_hash = rt_const_cstr("not_a_valid_hash");
        int8_t result = rt_password_verify(password, invalid_hash);
        test_result("Invalid hash format returns 0", result == 0);
    }

    // Test 2: Missing prefix returns 0
    {
        rt_string password = rt_const_cstr("password");
        rt_string invalid_hash = rt_const_cstr("SHA256$100000$salt$hash");
        int8_t result = rt_password_verify(password, invalid_hash);
        test_result("Wrong prefix returns 0", result == 0);
    }

    // Test 3: Malformed hash (missing parts) returns 0
    {
        rt_string password = rt_const_cstr("password");
        rt_string invalid_hash = rt_const_cstr("PBKDF2$100000$salt");
        int8_t result = rt_password_verify(password, invalid_hash);
        test_result("Malformed hash returns 0", result == 0);
    }

    // Test 4: Excessive embedded iteration count is rejected
    {
        rt_string password = rt_const_cstr("password");
        rt_string invalid_hash = rt_const_cstr("PBKDF2$500000000$AAAAAAAAAAAAAAAAAAAAAA==$"
                                               "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=");
        int8_t result = rt_password_verify(password, invalid_hash);
        test_result("Excessive iterations return 0", result == 0);
    }

    // Test 5: NULL hash input returns 0 instead of crashing
    {
        rt_string password = rt_const_cstr("password");
        int8_t result = rt_password_verify(password, NULL);
        test_result("NULL hash input returns 0", result == 0);
    }

    // Test 6: NULL password input returns 0 instead of crashing
    {
        rt_string hash = rt_const_cstr("PBKDF2$100000$AAAAAAAAAAAAAAAAAAAAAA==$"
                                       "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=");
        int8_t result = rt_password_verify(NULL, hash);
        test_result("NULL password input returns 0", result == 0);
    }

    // Test 7: Malformed Base64 padding is rejected
    {
        rt_string password = rt_const_cstr("password");
        rt_string invalid_hash = rt_const_cstr("PBKDF2$100000$AAA=AAAAAAAAAAAAAAAAAAAA$"
                                               "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=");
        int8_t result = rt_password_verify(password, invalid_hash);
        test_result("Malformed salt base64 padding returns 0", result == 0);
    }

    // Test 8: Wrong decoded hash length is rejected before comparison
    {
        rt_string password = rt_const_cstr("password");
        rt_string invalid_hash = rt_const_cstr("PBKDF2$100000$AAAAAAAAAAAAAAAAAAAAAA==$"
                                               "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
        int8_t result = rt_password_verify(password, invalid_hash);
        test_result("Wrong hash base64 shape returns 0", result == 0);
    }

    // Test 9: Embedded NUL in stored hash is rejected
    {
        const char raw[] = "PBKDF2$100000$AAAAAAAAAAAAAAAAAAAAAA==$"
                           "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";
        char with_nul[sizeof(raw) + 5];
        memcpy(with_nul, raw, sizeof(raw) - 1);
        with_nul[sizeof(raw) - 1] = '\0';
        memcpy(with_nul + sizeof(raw), "junk", 5);
        rt_string password = rt_const_cstr("password");
        rt_string invalid_hash = make_string_raw(with_nul, sizeof(with_nul));
        int8_t result = rt_password_verify(password, invalid_hash);
        test_result("Stored hash with embedded NUL returns 0", result == 0);
    }

    // Test 10: NeedsRehash fully validates current-looking scrypt hashes
    {
        rt_string invalid_hash = rt_const_cstr("SCRYPT$14$8$1$AAA=AAAAAAAAAAAAAAAAAAAA$"
                                               "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=");
        test_result("Malformed current scrypt hash needs rehash",
                    rt_password_needs_rehash(invalid_hash) == 1);
    }

    // Test 11: Unsupported scrypt parameters return 0 instead of trapping
    {
        rt_string password = rt_const_cstr("password");
        rt_string hostile_hash = rt_const_cstr("SCRYPT$20$32$1$AAAAAAAAAAAAAAAAAAAAAA==$"
                                               "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=");
        test_result("Unsupported scrypt verify returns 0",
                    rt_password_verify(password, hostile_hash) == 0);
        test_result("Unsupported scrypt hash needs rehash",
                    rt_password_needs_rehash(hostile_hash) == 1);
    }

    // Test 12: Approved mode NeedsRehash fully validates current-looking PBKDF2 hashes
    {
        test_result("Enable approved mode for PBKDF2 rehash test",
                    rt_crypto_module_enable_approved_mode() == 1);
        rt_string malformed_hash = rt_const_cstr("PBKDF2$300000$AAA=AAAAAAAAAAAAAAAAAAAA$"
                                                 "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=");
        test_result("Malformed current PBKDF2 hash needs rehash in approved mode",
                    rt_password_needs_rehash(malformed_hash) == 1);
        rt_string noncanonical_hash = rt_const_cstr("PBKDF2$300000$AAAAAAAAAAAAAAAAAAAAAB==$"
                                                    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=");
        test_result("Non-canonical PBKDF2 base64 needs rehash in approved mode",
                    rt_password_needs_rehash(noncanonical_hash) == 1);
        rt_string valid_hash = rt_password_hash(rt_const_cstr("password"));
        test_result("Valid approved PBKDF2 hash is current",
                    rt_password_needs_rehash(valid_hash) == 0);
        rt_crypto_module_disable_approved_mode();
    }

    printf("\n");
}

//=============================================================================
// Entry Point
//=============================================================================

int main() {
    printf("=== RT Password Tests ===\n\n");

    test_password_hash_format();
    test_password_verify();
    test_password_invalid_input();

    printf("All Password tests passed!\n");
    return 0;
}
