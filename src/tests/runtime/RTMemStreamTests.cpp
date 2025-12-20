//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTMemStreamTests.cpp
// Purpose: Validate in-memory binary stream operations in rt_memstream.c.
// Key invariants: MemStream provides correct little-endian encoding,
//                 automatic growth, and proper position tracking.
// Links: docs/viperlib/io.md
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include "rt_memstream.h"
#include "rt_bytes.h"
#include "rt_string.h"

#include <cassert>
#include <cmath>
#include <csetjmp>
#include <cstdio>
#include <cstring>

namespace
{
static jmp_buf g_trap_jmp;
static const char *g_last_trap = nullptr;
static bool g_trap_expected = false;
} // namespace

extern "C" void vm_trap(const char *msg)
{
    g_last_trap = msg;
    if (g_trap_expected)
        longjmp(g_trap_jmp, 1);
    rt_abort(msg);
}

#define EXPECT_TRAP(expr)                                                                          \
    do                                                                                             \
    {                                                                                              \
        g_trap_expected = true;                                                                    \
        g_last_trap = nullptr;                                                                     \
        if (setjmp(g_trap_jmp) == 0)                                                               \
        {                                                                                          \
            expr;                                                                                  \
            assert(false && "Expected trap did not occur");                                        \
        }                                                                                          \
        g_trap_expected = false;                                                                   \
    } while (0)

/// @brief Helper to print test result.
static void test_result(const char *name, bool passed)
{
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
    assert(passed);
}

/// @brief Test basic stream creation.
static void test_create_empty()
{
    printf("Testing MemStream.New()...\n");

    void *ms = rt_memstream_new();
    assert(ms != nullptr);
    assert(rt_memstream_get_pos(ms) == 0);
    assert(rt_memstream_get_len(ms) == 0);

    test_result("Create empty stream", true);
}

/// @brief Test stream creation with capacity.
static void test_create_with_capacity()
{
    printf("Testing MemStream.NewCapacity()...\n");

    void *ms = rt_memstream_new_capacity(1024);
    assert(ms != nullptr);
    assert(rt_memstream_get_pos(ms) == 0);
    assert(rt_memstream_get_len(ms) == 0);
    assert(rt_memstream_get_capacity(ms) >= 1024);

    test_result("Create with capacity", true);
}

/// @brief Test stream creation from bytes.
static void test_from_bytes()
{
    printf("Testing MemStream.FromBytes()...\n");

    void *bytes = rt_bytes_new(4);
    rt_bytes_set(bytes, 0, 0x12);
    rt_bytes_set(bytes, 1, 0x34);
    rt_bytes_set(bytes, 2, 0x56);
    rt_bytes_set(bytes, 3, 0x78);

    void *ms = rt_memstream_from_bytes(bytes);
    assert(ms != nullptr);
    assert(rt_memstream_get_pos(ms) == 0);
    assert(rt_memstream_get_len(ms) == 4);

    // Read back the data
    assert(rt_memstream_read_u8(ms) == 0x12);
    assert(rt_memstream_read_u8(ms) == 0x34);
    assert(rt_memstream_read_u8(ms) == 0x56);
    assert(rt_memstream_read_u8(ms) == 0x78);

    test_result("From bytes", true);
}

/// @brief Test 8-bit integer read/write.
static void test_i8_u8()
{
    printf("Testing I8/U8...\n");

    void *ms = rt_memstream_new();

    // Write signed and unsigned bytes
    rt_memstream_write_i8(ms, -128);
    rt_memstream_write_i8(ms, 127);
    rt_memstream_write_u8(ms, 0);
    rt_memstream_write_u8(ms, 255);

    assert(rt_memstream_get_len(ms) == 4);
    assert(rt_memstream_get_pos(ms) == 4);

    // Read back
    rt_memstream_set_pos(ms, 0);
    assert(rt_memstream_read_i8(ms) == -128);
    assert(rt_memstream_read_i8(ms) == 127);
    assert(rt_memstream_read_u8(ms) == 0);
    assert(rt_memstream_read_u8(ms) == 255);

    test_result("I8/U8", true);
}

/// @brief Test 16-bit integer read/write.
static void test_i16_u16()
{
    printf("Testing I16/U16...\n");

    void *ms = rt_memstream_new();

    rt_memstream_write_i16(ms, -32768);
    rt_memstream_write_i16(ms, 32767);
    rt_memstream_write_u16(ms, 0);
    rt_memstream_write_u16(ms, 65535);

    assert(rt_memstream_get_len(ms) == 8);

    rt_memstream_set_pos(ms, 0);
    assert(rt_memstream_read_i16(ms) == -32768);
    assert(rt_memstream_read_i16(ms) == 32767);
    assert(rt_memstream_read_u16(ms) == 0);
    assert(rt_memstream_read_u16(ms) == 65535);

    test_result("I16/U16", true);
}

/// @brief Test 32-bit integer read/write.
static void test_i32_u32()
{
    printf("Testing I32/U32...\n");

    void *ms = rt_memstream_new();

    rt_memstream_write_i32(ms, -2147483648LL);
    rt_memstream_write_i32(ms, 2147483647);
    rt_memstream_write_u32(ms, 0);
    rt_memstream_write_u32(ms, 4294967295ULL);

    assert(rt_memstream_get_len(ms) == 16);

    rt_memstream_set_pos(ms, 0);
    assert(rt_memstream_read_i32(ms) == -2147483648LL);
    assert(rt_memstream_read_i32(ms) == 2147483647);
    assert(rt_memstream_read_u32(ms) == 0);
    assert(rt_memstream_read_u32(ms) == 4294967295LL);

    test_result("I32/U32", true);
}

/// @brief Test 64-bit integer read/write.
static void test_i64()
{
    printf("Testing I64...\n");

    void *ms = rt_memstream_new();

    rt_memstream_write_i64(ms, INT64_MIN);
    rt_memstream_write_i64(ms, INT64_MAX);
    rt_memstream_write_i64(ms, 0x123456789ABCDEF0LL);

    assert(rt_memstream_get_len(ms) == 24);

    rt_memstream_set_pos(ms, 0);
    assert(rt_memstream_read_i64(ms) == INT64_MIN);
    assert(rt_memstream_read_i64(ms) == INT64_MAX);
    assert(rt_memstream_read_i64(ms) == 0x123456789ABCDEF0LL);

    test_result("I64", true);
}

/// @brief Test float read/write.
static void test_floats()
{
    printf("Testing F32/F64...\n");

    void *ms = rt_memstream_new();

    rt_memstream_write_f32(ms, 3.14159f);
    rt_memstream_write_f32(ms, -1.0f);
    rt_memstream_write_f64(ms, 2.718281828459045);
    rt_memstream_write_f64(ms, -1e100);

    assert(rt_memstream_get_len(ms) == 24);

    rt_memstream_set_pos(ms, 0);

    // F32 has limited precision
    double f32_1 = rt_memstream_read_f32(ms);
    assert(fabs(f32_1 - 3.14159) < 1e-5);

    double f32_2 = rt_memstream_read_f32(ms);
    assert(f32_2 == -1.0);

    // F64 has full precision
    double f64_1 = rt_memstream_read_f64(ms);
    assert(fabs(f64_1 - 2.718281828459045) < 1e-15);

    double f64_2 = rt_memstream_read_f64(ms);
    assert(f64_2 == -1e100);

    test_result("F32/F64", true);
}

/// @brief Test bytes read/write.
static void test_bytes()
{
    printf("Testing ReadBytes/WriteBytes...\n");

    void *ms = rt_memstream_new();

    // Create some bytes to write
    void *bytes = rt_bytes_new(5);
    rt_bytes_set(bytes, 0, 'H');
    rt_bytes_set(bytes, 1, 'e');
    rt_bytes_set(bytes, 2, 'l');
    rt_bytes_set(bytes, 3, 'l');
    rt_bytes_set(bytes, 4, 'o');

    rt_memstream_write_bytes(ms, bytes);
    assert(rt_memstream_get_len(ms) == 5);

    rt_memstream_set_pos(ms, 0);
    void *read_bytes = rt_memstream_read_bytes(ms, 5);
    assert(rt_bytes_len(read_bytes) == 5);
    assert(rt_bytes_get(read_bytes, 0) == 'H');
    assert(rt_bytes_get(read_bytes, 4) == 'o');

    test_result("ReadBytes/WriteBytes", true);
}

/// @brief Test string read/write.
static void test_strings()
{
    printf("Testing ReadStr/WriteStr...\n");

    void *ms = rt_memstream_new();

    rt_string str = rt_string_from_bytes("Hello, World!", 13);
    rt_memstream_write_str(ms, str);
    assert(rt_memstream_get_len(ms) == 13);

    rt_memstream_set_pos(ms, 0);
    void *read_str = rt_memstream_read_str(ms, 13);
    const char *cstr = rt_string_cstr((rt_string)read_str);
    assert(strcmp(cstr, "Hello, World!") == 0);

    test_result("ReadStr/WriteStr", true);
}

/// @brief Test ToBytes.
static void test_to_bytes()
{
    printf("Testing ToBytes...\n");

    void *ms = rt_memstream_new();
    rt_memstream_write_u8(ms, 0xCA);
    rt_memstream_write_u8(ms, 0xFE);
    rt_memstream_write_u8(ms, 0xBA);
    rt_memstream_write_u8(ms, 0xBE);

    void *bytes = rt_memstream_to_bytes(ms);
    assert(rt_bytes_len(bytes) == 4);
    assert(rt_bytes_get(bytes, 0) == 0xCA);
    assert(rt_bytes_get(bytes, 1) == 0xFE);
    assert(rt_bytes_get(bytes, 2) == 0xBA);
    assert(rt_bytes_get(bytes, 3) == 0xBE);

    test_result("ToBytes", true);
}

/// @brief Test Clear.
static void test_clear()
{
    printf("Testing Clear...\n");

    void *ms = rt_memstream_new();
    rt_memstream_write_i64(ms, 12345);
    assert(rt_memstream_get_len(ms) == 8);
    assert(rt_memstream_get_pos(ms) == 8);

    rt_memstream_clear(ms);
    assert(rt_memstream_get_len(ms) == 0);
    assert(rt_memstream_get_pos(ms) == 0);

    test_result("Clear", true);
}

/// @brief Test Seek and Skip.
static void test_seek_skip()
{
    printf("Testing Seek/Skip...\n");

    void *ms = rt_memstream_new();
    rt_memstream_write_i64(ms, 1);
    rt_memstream_write_i64(ms, 2);
    rt_memstream_write_i64(ms, 3);

    // Seek to beginning
    rt_memstream_seek(ms, 0);
    assert(rt_memstream_get_pos(ms) == 0);

    // Skip 8 bytes
    rt_memstream_skip(ms, 8);
    assert(rt_memstream_get_pos(ms) == 8);
    assert(rt_memstream_read_i64(ms) == 2);

    // Seek to specific position
    rt_memstream_seek(ms, 16);
    assert(rt_memstream_read_i64(ms) == 3);

    test_result("Seek/Skip", true);
}

/// @brief Test position property.
static void test_pos_property()
{
    printf("Testing Pos property...\n");

    void *ms = rt_memstream_new();
    rt_memstream_write_i32(ms, 100);
    rt_memstream_write_i32(ms, 200);
    rt_memstream_write_i32(ms, 300);

    // Read from middle
    rt_memstream_set_pos(ms, 4);
    assert(rt_memstream_get_pos(ms) == 4);
    assert(rt_memstream_read_i32(ms) == 200);
    assert(rt_memstream_get_pos(ms) == 8);

    test_result("Pos property", true);
}

/// @brief Test automatic growth.
static void test_auto_growth()
{
    printf("Testing automatic growth...\n");

    void *ms = rt_memstream_new();

    // Write a lot of data to force growth
    for (int i = 0; i < 1000; i++)
    {
        rt_memstream_write_i32(ms, i);
    }

    assert(rt_memstream_get_len(ms) == 4000);
    assert(rt_memstream_get_capacity(ms) >= 4000);

    // Verify data
    rt_memstream_set_pos(ms, 0);
    for (int i = 0; i < 1000; i++)
    {
        assert(rt_memstream_read_i32(ms) == i);
    }

    test_result("Auto growth", true);
}

/// @brief Test position beyond length.
static void test_pos_beyond_len()
{
    printf("Testing position beyond length...\n");

    void *ms = rt_memstream_new();
    rt_memstream_write_u8(ms, 0xAA);

    // Set position beyond length
    rt_memstream_set_pos(ms, 10);
    rt_memstream_write_u8(ms, 0xBB);

    // Length should now be 11
    assert(rt_memstream_get_len(ms) == 11);

    // Read back - gap should be zeros
    rt_memstream_set_pos(ms, 0);
    assert(rt_memstream_read_u8(ms) == 0xAA);
    for (int i = 1; i < 10; i++)
    {
        assert(rt_memstream_read_u8(ms) == 0);
    }
    assert(rt_memstream_read_u8(ms) == 0xBB);

    test_result("Position beyond length", true);
}

/// @brief Test reading past end traps.
static void test_read_past_end()
{
    printf("Testing read past end traps...\n");

    void *ms = rt_memstream_new();
    rt_memstream_write_u8(ms, 0x42);
    rt_memstream_set_pos(ms, 0);

    // Read one byte - should succeed
    assert(rt_memstream_read_u8(ms) == 0x42);

    // Read another - should trap
    EXPECT_TRAP(rt_memstream_read_u8(ms));

    test_result("Read past end traps", true);
}

/// @brief Test negative position traps.
static void test_negative_pos()
{
    printf("Testing negative position traps...\n");

    void *ms = rt_memstream_new();

    EXPECT_TRAP(rt_memstream_set_pos(ms, -1));

    test_result("Negative position traps", true);
}

/// @brief Test little-endian encoding.
static void test_little_endian()
{
    printf("Testing little-endian encoding...\n");

    void *ms = rt_memstream_new();

    // Write 0x12345678 as I32
    rt_memstream_write_i32(ms, 0x12345678);

    // Read individual bytes
    rt_memstream_set_pos(ms, 0);
    assert(rt_memstream_read_u8(ms) == 0x78); // LSB first
    assert(rt_memstream_read_u8(ms) == 0x56);
    assert(rt_memstream_read_u8(ms) == 0x34);
    assert(rt_memstream_read_u8(ms) == 0x12); // MSB last

    test_result("Little-endian encoding", true);
}

int main()
{
    printf("=== MemStream Runtime Tests ===\n");

    test_create_empty();
    test_create_with_capacity();
    test_from_bytes();
    test_i8_u8();
    test_i16_u16();
    test_i32_u32();
    test_i64();
    test_floats();
    test_bytes();
    test_strings();
    test_to_bytes();
    test_clear();
    test_seek_skip();
    test_pos_property();
    test_auto_growth();
    test_pos_beyond_len();
    test_read_past_end();
    test_negative_pos();
    test_little_endian();

    printf("\nAll MemStream tests passed!\n");
    return 0;
}
