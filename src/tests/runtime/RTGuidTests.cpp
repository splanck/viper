//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTGuidTests.cpp
// Purpose: Validate Viper.Text.Guid runtime functions.
// Key invariants: New() generates valid format, unique values on successive calls,
//                 IsValid() correctly identifies valid/invalid GUIDs, ToBytes/FromBytes
//                 roundtrip correctly.
// Links: docs/viperlib.md

#include "rt_bytes.h"
#include "rt_guid.h"
#include "rt_string.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

/// @brief Helper to print test result.
static void test_result(const char *name, bool passed)
{
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
    assert(passed);
}

/// @brief Test that New() generates valid format.
static void test_new_format()
{
    printf("Testing Guid.New() format:\n");

    rt_string guid = rt_guid_new();
    const char *str = rt_string_cstr(guid);

    test_result("New() returns non-null", guid != nullptr);
    test_result("New() length is 36", strlen(str) == 36);

    // Check dash positions: 8, 13, 18, 23
    test_result("Dash at position 8", str[8] == '-');
    test_result("Dash at position 13", str[13] == '-');
    test_result("Dash at position 18", str[18] == '-');
    test_result("Dash at position 23", str[23] == '-');

    // Check hex digits at other positions
    bool all_hex = true;
    for (int i = 0; i < 36; i++)
    {
        if (i == 8 || i == 13 || i == 18 || i == 23)
            continue;
        char c = str[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
        {
            all_hex = false;
            break;
        }
    }
    test_result("All non-dash chars are lowercase hex", all_hex);

    // Check version 4 indicator (position 14 should be '4')
    test_result("Version indicator is '4'", str[14] == '4');

    // Check variant bits (position 19 should be 8, 9, a, or b)
    char variant = str[19];
    test_result("Variant indicator is valid (8,9,a,b)",
                variant == '8' || variant == '9' || variant == 'a' || variant == 'b');

    printf("\n");
}

/// @brief Test that New() generates unique values.
static void test_new_uniqueness()
{
    printf("Testing Guid.New() uniqueness:\n");

    std::set<std::string> guids;
    const int count = 100;

    for (int i = 0; i < count; i++)
    {
        rt_string guid = rt_guid_new();
        guids.insert(rt_string_cstr(guid));
    }

    test_result("100 calls generate 100 unique GUIDs", guids.size() == count);

    printf("\n");
}

/// @brief Test Empty property.
static void test_empty()
{
    printf("Testing Guid.Empty:\n");

    rt_string empty = rt_guid_empty();
    const char *str = rt_string_cstr(empty);

    test_result("Empty returns non-null", empty != nullptr);
    test_result("Empty is all zeros", strcmp(str, "00000000-0000-0000-0000-000000000000") == 0);

    // Empty should be valid format
    test_result("Empty is valid format", rt_guid_is_valid(empty) != 0);

    printf("\n");
}

/// @brief Test IsValid() with valid GUIDs.
static void test_is_valid_positive()
{
    printf("Testing Guid.IsValid() positive cases:\n");

    // Test with generated GUID
    rt_string guid = rt_guid_new();
    test_result("Generated GUID is valid", rt_guid_is_valid(guid) != 0);

    // Test with empty GUID
    rt_string empty = rt_guid_empty();
    test_result("Empty GUID is valid", rt_guid_is_valid(empty) != 0);

    // Test with known valid GUIDs
    rt_string valid1 = rt_const_cstr("12345678-1234-1234-1234-123456789abc");
    test_result("Known valid GUID 1", rt_guid_is_valid(valid1) != 0);

    rt_string valid2 = rt_const_cstr("abcdef01-2345-6789-abcd-ef0123456789");
    test_result("Known valid GUID 2", rt_guid_is_valid(valid2) != 0);

    // Case should not matter for validation
    rt_string uppercase = rt_const_cstr("12345678-ABCD-EFAB-CDEF-123456789ABC");
    test_result("Uppercase GUID is valid", rt_guid_is_valid(uppercase) != 0);

    printf("\n");
}

/// @brief Test IsValid() with invalid GUIDs.
static void test_is_valid_negative()
{
    printf("Testing Guid.IsValid() negative cases:\n");

    // Too short
    rt_string short_str = rt_const_cstr("12345678-1234-1234-1234-12345678");
    test_result("Too short is invalid", rt_guid_is_valid(short_str) == 0);

    // Too long
    rt_string long_str = rt_const_cstr("12345678-1234-1234-1234-123456789abcdef");
    test_result("Too long is invalid", rt_guid_is_valid(long_str) == 0);

    // Wrong dash positions
    rt_string wrong_dash = rt_const_cstr("1234567-81234-1234-1234-123456789abc");
    test_result("Wrong dash position is invalid", rt_guid_is_valid(wrong_dash) == 0);

    // Missing dashes
    rt_string no_dashes = rt_const_cstr("1234567812341234123412345678abcd");
    test_result("No dashes is invalid", rt_guid_is_valid(no_dashes) == 0);

    // Non-hex characters
    rt_string non_hex = rt_const_cstr("12345678-1234-1234-1234-12345678ghij");
    test_result("Non-hex chars is invalid", rt_guid_is_valid(non_hex) == 0);

    // Empty string
    rt_string empty_str = rt_const_cstr("");
    test_result("Empty string is invalid", rt_guid_is_valid(empty_str) == 0);

    // Null
    test_result("Null is invalid", rt_guid_is_valid(nullptr) == 0);

    printf("\n");
}

/// @brief Test ToBytes/FromBytes roundtrip.
static void test_bytes_roundtrip()
{
    printf("Testing ToBytes/FromBytes roundtrip:\n");

    // Generate a GUID and convert to bytes
    rt_string guid1 = rt_guid_new();
    void *bytes = rt_guid_to_bytes(guid1);

    test_result("ToBytes returns non-null", bytes != nullptr);
    test_result("ToBytes returns 16 bytes", rt_bytes_len(bytes) == 16);

    // Convert back to string
    rt_string guid2 = rt_guid_from_bytes(bytes);
    test_result("FromBytes returns non-null", guid2 != nullptr);
    test_result("Roundtrip preserves value",
                strcmp(rt_string_cstr(guid1), rt_string_cstr(guid2)) == 0);

    // Test with known GUID
    rt_string known = rt_const_cstr("12345678-abcd-ef01-2345-6789abcdef01");
    void *known_bytes = rt_guid_to_bytes(known);

    // Verify specific byte values
    // Format: XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX
    // Bytes:  0-3      4-5  6-7  8-9  10-15
    test_result("Byte 0 = 0x12", rt_bytes_get(known_bytes, 0) == 0x12);
    test_result("Byte 1 = 0x34", rt_bytes_get(known_bytes, 1) == 0x34);
    test_result("Byte 2 = 0x56", rt_bytes_get(known_bytes, 2) == 0x56);
    test_result("Byte 3 = 0x78", rt_bytes_get(known_bytes, 3) == 0x78);
    test_result("Byte 4 = 0xab", rt_bytes_get(known_bytes, 4) == 0xab);
    test_result("Byte 5 = 0xcd", rt_bytes_get(known_bytes, 5) == 0xcd);
    test_result("Byte 6 = 0xef", rt_bytes_get(known_bytes, 6) == 0xef);
    test_result("Byte 7 = 0x01", rt_bytes_get(known_bytes, 7) == 0x01);

    rt_string known_rt = rt_guid_from_bytes(known_bytes);
    test_result("Known GUID roundtrips correctly",
                strcmp(rt_string_cstr(known), rt_string_cstr(known_rt)) == 0);

    printf("\n");
}

/// @brief Test FromBytes with manually constructed bytes.
static void test_from_bytes_manual()
{
    printf("Testing FromBytes with manual bytes:\n");

    // Create bytes manually
    void *bytes = rt_bytes_new(16);
    for (int i = 0; i < 16; i++)
    {
        rt_bytes_set(bytes, i, i * 17); // 0x00, 0x11, 0x22, ...
    }

    rt_string guid = rt_guid_from_bytes(bytes);
    const char *str = rt_string_cstr(guid);

    test_result("Manual bytes creates valid GUID", rt_guid_is_valid(guid) != 0);

    // Expected: 00112233-4455-6677-8899-aabbccddeeff
    test_result("Manual bytes creates expected GUID",
                strcmp(str, "00112233-4455-6677-8899-aabbccddeeff") == 0);

    printf("\n");
}

/// @brief Test multiple consecutive generations maintain proper format.
static void test_consecutive_generations()
{
    printf("Testing consecutive generations:\n");

    bool all_valid = true;
    for (int i = 0; i < 50; i++)
    {
        rt_string guid = rt_guid_new();
        if (rt_guid_is_valid(guid) == 0)
        {
            all_valid = false;
            break;
        }

        const char *str = rt_string_cstr(guid);
        // Verify version 4
        if (str[14] != '4')
        {
            all_valid = false;
            break;
        }

        // Verify variant
        char v = str[19];
        if (!(v == '8' || v == '9' || v == 'a' || v == 'b'))
        {
            all_valid = false;
            break;
        }
    }

    test_result("50 consecutive GUIDs all valid with correct version/variant", all_valid);

    printf("\n");
}

/// @brief Entry point for Guid tests.
int main()
{
    printf("=== RT Guid Tests ===\n\n");

    test_new_format();
    test_new_uniqueness();
    test_empty();
    test_is_valid_positive();
    test_is_valid_negative();
    test_bytes_roundtrip();
    test_from_bytes_manual();
    test_consecutive_generations();

    printf("All Guid tests passed!\n");
    return 0;
}
