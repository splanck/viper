//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTPasswordTests.cpp
// Purpose: Validate password hashing and verification functions.
// Key invariants: Hash produces valid format; Verify matches correctly.
//
//===----------------------------------------------------------------------===//

#include "rt_password.h"
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

//=============================================================================
// Password Hash Format Tests
//=============================================================================

static void test_password_hash_format()
{
    printf("Testing Password.Hash format:\n");

    // Test 1: Hash produces valid format "PBKDF2$iterations$salt_b64$hash_b64"
    {
        rt_string password = rt_const_cstr("mypassword123");
        rt_string hash = rt_password_hash(password);
        const char *hash_str = rt_string_cstr(hash);

        // Should start with "PBKDF2$"
        test_result("Hash starts with PBKDF2$", strncmp(hash_str, "PBKDF2$", 7) == 0);

        // Count $ delimiters (should be exactly 3)
        int count = 0;
        for (const char *p = hash_str; *p; p++)
        {
            if (*p == '$')
                count++;
        }
        test_result("Hash has 3 delimiters", count == 3);
    }

    // Test 2: Hash with custom iterations
    {
        rt_string password = rt_const_cstr("testpass");
        rt_string hash = rt_password_hash_with_iterations(password, 50000);
        const char *hash_str = rt_string_cstr(hash);

        // Should contain "50000" as the iteration count
        test_result("Hash contains custom iterations", strstr(hash_str, "$50000$") != nullptr);
    }

    // Test 3: Different passwords produce different hashes
    {
        rt_string pwd1 = rt_const_cstr("password1");
        rt_string pwd2 = rt_const_cstr("password2");
        rt_string hash1 = rt_password_hash(pwd1);
        rt_string hash2 = rt_password_hash(pwd2);

        test_result("Different passwords produce different hashes",
                    strcmp(rt_string_cstr(hash1), rt_string_cstr(hash2)) != 0);
    }

    // Test 4: Same password produces different hashes (due to random salt)
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

static void test_password_verify()
{
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
        rt_string password = rt_const_cstr(
            "This is a very long password that exceeds the normal length "
            "that most people would use for their passwords, but it should "
            "still work correctly with the PBKDF2 algorithm.");
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
        rt_string hash = rt_password_hash_with_iterations(password, 20000);
        int8_t result = rt_password_verify(password, hash);
        test_result("Custom iteration hash verifies", result == 1);
    }

    printf("\n");
}

//=============================================================================
// Invalid Input Tests
//=============================================================================

static void test_password_invalid_input()
{
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

    printf("\n");
}

//=============================================================================
// Entry Point
//=============================================================================

int main()
{
    printf("=== RT Password Tests ===\n\n");

    test_password_hash_format();
    test_password_verify();
    test_password_invalid_input();

    printf("All Password tests passed!\n");
    return 0;
}
