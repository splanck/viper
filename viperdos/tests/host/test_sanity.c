/**
 * @file test_sanity.c
 * @brief Basic sanity test for the host test framework.
 *
 * This test verifies that:
 * 1. The test framework compiles and runs correctly
 * 2. Basic C operations work on the host
 * 3. The test harness is properly wired up
 */

#include "test_framework.h"

int main(void) {
    TEST_BEGIN("Host Sanity Tests");

    TEST_SECTION("Basic assertions");
    TEST_ASSERT(1 == 1, "1 equals 1");
    TEST_ASSERT(0 == 0, "0 equals 0");
    TEST_ASSERT(1 != 0, "1 not equal to 0");

    TEST_SECTION("Integer equality");
    TEST_ASSERT_EQ(42, 42, "42 == 42");
    TEST_ASSERT_EQ(-1, -1, "-1 == -1");
    TEST_ASSERT_EQ(0, 0, "0 == 0");
    TEST_ASSERT_NE(1, 2, "1 != 2");

    TEST_SECTION("String operations");
    TEST_ASSERT_STR_EQ("hello", "hello", "string equality");
    TEST_ASSERT_STR_EQ("", "", "empty string equality");

    TEST_SECTION("Pointer checks");
    int x = 42;
    int *ptr = &x;
    int *null_ptr = NULL;
    TEST_ASSERT_NOT_NULL(ptr, "valid pointer is not null");
    TEST_ASSERT_NULL(null_ptr, "null pointer is null");

    TEST_SECTION("Arithmetic");
    TEST_ASSERT_EQ(2 + 2, 4, "2 + 2 = 4");
    TEST_ASSERT_EQ(10 - 3, 7, "10 - 3 = 7");
    TEST_ASSERT_EQ(6 * 7, 42, "6 * 7 = 42");
    TEST_ASSERT_EQ(100 / 10, 10, "100 / 10 = 10");
    TEST_ASSERT_EQ(17 % 5, 2, "17 % 5 = 2");

    TEST_SECTION("Bit operations");
    TEST_ASSERT_EQ(0xFF & 0x0F, 0x0F, "0xFF & 0x0F = 0x0F");
    TEST_ASSERT_EQ(0xF0 | 0x0F, 0xFF, "0xF0 | 0x0F = 0xFF");
    TEST_ASSERT_EQ(0xFF ^ 0x0F, 0xF0, "0xFF ^ 0x0F = 0xF0");
    TEST_ASSERT_EQ(1 << 4, 16, "1 << 4 = 16");
    TEST_ASSERT_EQ(16 >> 2, 4, "16 >> 2 = 4");

    TEST_END();
}
