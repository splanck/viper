//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTBinaryBufferTests.cpp
// Purpose: Comprehensive tests for the BinaryBuffer runtime type.
//          Covers constructors, write/read round-trips, cursor semantics,
//          capacity growth, to_bytes/from_bytes paths, and reset behaviour.
//
//===----------------------------------------------------------------------===//

#include "rt_binbuf.h"
#include "rt_bytes.h"
#include "rt_internal.h"
#include "rt_string.h"

#include <cassert>
#include <csetjmp>
#include <cstring>

//=============================================================================
// Trap infrastructure
//=============================================================================

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
            (void)(expr);                                                                          \
            assert(false && "Expected trap did not occur");                                        \
        }                                                                                          \
        g_trap_expected = false;                                                                   \
    } while (0)

//=============================================================================
// Construction
//=============================================================================

static void test_new_default()
{
    void *bb = rt_binbuf_new();
    assert(bb != nullptr);
    assert(rt_binbuf_get_len(bb) == 0);
    assert(rt_binbuf_get_position(bb) == 0);
}

static void test_new_cap()
{
    void *bb = rt_binbuf_new_cap(1024);
    assert(bb != nullptr);
    assert(rt_binbuf_get_len(bb) == 0);
    assert(rt_binbuf_get_position(bb) == 0);
}

static void test_new_cap_clamped()
{
    // Negative capacity is clamped to 1
    void *bb = rt_binbuf_new_cap(-5);
    assert(bb != nullptr);
    assert(rt_binbuf_get_len(bb) == 0);
}

static void test_from_bytes()
{
    // Build a bytes object [10, 20, 30]
    void *src = rt_bytes_new(3);
    rt_bytes_set(src, 0, 10);
    rt_bytes_set(src, 1, 20);
    rt_bytes_set(src, 2, 30);

    void *bb = rt_binbuf_from_bytes(src);
    assert(bb != nullptr);
    assert(rt_binbuf_get_len(bb) == 3);
    assert(rt_binbuf_get_position(bb) == 0);

    // Verify content via read (exercises IO-H-2 fixed path)
    assert(rt_binbuf_read_byte(bb) == 10);
    assert(rt_binbuf_read_byte(bb) == 20);
    assert(rt_binbuf_read_byte(bb) == 30);
}

static void test_from_bytes_null()
{
    // NULL input → empty buffer
    void *bb = rt_binbuf_from_bytes(nullptr);
    assert(bb != nullptr);
    assert(rt_binbuf_get_len(bb) == 0);
}

//=============================================================================
// Write / Read Round-Trips
//=============================================================================

static void test_write_read_byte()
{
    void *bb = rt_binbuf_new();
    rt_binbuf_write_byte(bb, 0xAB);
    assert(rt_binbuf_get_len(bb) == 1);

    rt_binbuf_set_position(bb, 0);
    assert(rt_binbuf_read_byte(bb) == 0xAB);
}

static void test_write_read_i16le()
{
    void *bb = rt_binbuf_new();
    rt_binbuf_write_i16le(bb, 0x1234);
    rt_binbuf_set_position(bb, 0);
    assert(rt_binbuf_read_i16le(bb) == 0x1234);
}

static void test_write_read_i16be()
{
    void *bb = rt_binbuf_new();
    rt_binbuf_write_i16be(bb, 0x5678);
    rt_binbuf_set_position(bb, 0);
    assert(rt_binbuf_read_i16be(bb) == 0x5678);
}

static void test_write_read_i32le()
{
    void *bb = rt_binbuf_new();
    rt_binbuf_write_i32le(bb, 0x12345678);
    rt_binbuf_set_position(bb, 0);
    assert(rt_binbuf_read_i32le(bb) == 0x12345678);
}

static void test_write_read_i32be()
{
    void *bb = rt_binbuf_new();
    rt_binbuf_write_i32be(bb, 0xDEADBEEF);
    rt_binbuf_set_position(bb, 0);
    assert(rt_binbuf_read_i32be(bb) == (int64_t)0xDEADBEEF);
}

static void test_write_read_i64le()
{
    void *bb = rt_binbuf_new();
    rt_binbuf_write_i64le(bb, 0x0123456789ABCDEFLL);
    rt_binbuf_set_position(bb, 0);
    assert(rt_binbuf_read_i64le(bb) == 0x0123456789ABCDEFLL);
}

static void test_write_read_i64be()
{
    void *bb = rt_binbuf_new();
    rt_binbuf_write_i64be(bb, 0xFEDCBA9876543210LL);
    rt_binbuf_set_position(bb, 0);
    assert(rt_binbuf_read_i64be(bb) == (int64_t)0xFEDCBA9876543210LL);
}

static void test_endian_byte_order_i16()
{
    // Verify LE places low byte first, BE places high byte first
    void *le = rt_binbuf_new();
    rt_binbuf_write_i16le(le, 0x0102);
    rt_binbuf_set_position(le, 0);
    assert(rt_binbuf_read_byte(le) == 0x02); // low byte first
    assert(rt_binbuf_read_byte(le) == 0x01); // high byte second

    void *be = rt_binbuf_new();
    rt_binbuf_write_i16be(be, 0x0102);
    rt_binbuf_set_position(be, 0);
    assert(rt_binbuf_read_byte(be) == 0x01); // high byte first
    assert(rt_binbuf_read_byte(be) == 0x02); // low byte second
}

static void test_write_read_str()
{
    void *bb = rt_binbuf_new();
    rt_string s = rt_const_cstr("hello");
    rt_binbuf_write_str(bb, s);

    // Verify: 4-byte LE length prefix + 5 bytes payload
    assert(rt_binbuf_get_len(bb) == 4 + 5);

    rt_binbuf_set_position(bb, 0);
    rt_string out = rt_binbuf_read_str(bb);
    assert(out != nullptr);
    assert(strcmp(rt_string_cstr(out), "hello") == 0);
}

static void test_write_read_bytes()
{
    void *src = rt_bytes_new(4);
    rt_bytes_set(src, 0, 0xDE);
    rt_bytes_set(src, 1, 0xAD);
    rt_bytes_set(src, 2, 0xBE);
    rt_bytes_set(src, 3, 0xEF);

    void *bb = rt_binbuf_new();
    rt_binbuf_write_bytes(bb, src);

    // 4-byte LE length prefix + 4 bytes payload
    assert(rt_binbuf_get_len(bb) == 4 + 4);

    rt_binbuf_set_position(bb, 0);
    int64_t prefix = rt_binbuf_read_i32le(bb); // read the length prefix
    assert(prefix == 4);
    void *out = rt_binbuf_read_bytes(bb, 4);
    assert(rt_bytes_get(out, 0) == 0xDE);
    assert(rt_bytes_get(out, 1) == 0xAD);
    assert(rt_bytes_get(out, 2) == 0xBE);
    assert(rt_bytes_get(out, 3) == 0xEF);
}

//=============================================================================
// Cursor / Position Semantics
//=============================================================================

static void test_position_advances_on_write()
{
    void *bb = rt_binbuf_new();
    assert(rt_binbuf_get_position(bb) == 0);
    rt_binbuf_write_byte(bb, 1);
    assert(rt_binbuf_get_position(bb) == 1);
    rt_binbuf_write_i32le(bb, 0);
    assert(rt_binbuf_get_position(bb) == 5);
}

static void test_position_advances_on_read()
{
    void *bb = rt_binbuf_new();
    rt_binbuf_write_byte(bb, 42);
    rt_binbuf_write_byte(bb, 99);
    rt_binbuf_set_position(bb, 0);
    assert(rt_binbuf_get_position(bb) == 0);
    rt_binbuf_read_byte(bb);
    assert(rt_binbuf_get_position(bb) == 1);
}

static void test_set_position_clamps_to_len()
{
    void *bb = rt_binbuf_new();
    rt_binbuf_write_byte(bb, 1);
    rt_binbuf_write_byte(bb, 2);

    rt_binbuf_set_position(bb, 100);         // beyond len
    assert(rt_binbuf_get_position(bb) == 2); // clamped to len

    rt_binbuf_set_position(bb, -5);          // negative
    assert(rt_binbuf_get_position(bb) == 0); // clamped to 0
}

static void test_read_past_end_traps()
{
    void *bb = rt_binbuf_new();
    rt_binbuf_write_byte(bb, 0xFF);
    rt_binbuf_set_position(bb, 0);
    rt_binbuf_read_byte(bb);
    // Position is now at end — next read must trap
    EXPECT_TRAP(rt_binbuf_read_byte(bb));
}

//=============================================================================
// to_bytes / from_bytes Round-Trip
//=============================================================================

static void test_to_bytes_round_trip()
{
    void *bb = rt_binbuf_new();
    for (int i = 0; i < 8; i++)
        rt_binbuf_write_byte(bb, (int64_t)(i * 10));

    // to_bytes converts buffer contents to a Bytes object (IO-M-2 fixed path)
    void *bytes = rt_binbuf_to_bytes(bb);
    assert(rt_bytes_len(bytes) == 8);
    for (int i = 0; i < 8; i++)
        assert(rt_bytes_get(bytes, i) == (int64_t)(i * 10));
}

static void test_from_bytes_to_bytes_identity()
{
    // Build source bytes
    void *src = rt_bytes_new(5);
    for (int i = 0; i < 5; i++)
        rt_bytes_set(src, i, (int64_t)(100 + i));

    // Round-trip: bytes → binbuf → bytes
    void *bb = rt_binbuf_from_bytes(src);
    void *dst = rt_binbuf_to_bytes(bb);

    assert(rt_bytes_len(dst) == 5);
    for (int i = 0; i < 5; i++)
        assert(rt_bytes_get(dst, i) == (int64_t)(100 + i));
}

//=============================================================================
// Reset
//=============================================================================

static void test_reset()
{
    void *bb = rt_binbuf_new();
    rt_binbuf_write_byte(bb, 1);
    rt_binbuf_write_byte(bb, 2);
    assert(rt_binbuf_get_len(bb) == 2);
    assert(rt_binbuf_get_position(bb) == 2);

    rt_binbuf_reset(bb);
    assert(rt_binbuf_get_len(bb) == 0);
    assert(rt_binbuf_get_position(bb) == 0);

    // Buffer can be reused after reset
    rt_binbuf_write_byte(bb, 99);
    assert(rt_binbuf_get_len(bb) == 1);
    rt_binbuf_set_position(bb, 0);
    assert(rt_binbuf_read_byte(bb) == 99);
}

//=============================================================================
// Capacity Growth (exercises IO-H-3 overflow guard in binbuf_ensure)
//=============================================================================

static void test_capacity_growth()
{
    // Start with a tiny buffer and write enough to force several doublings
    void *bb = rt_binbuf_new_cap(1);

    const int N = 1024;
    for (int i = 0; i < N; i++)
        rt_binbuf_write_byte(bb, (int64_t)(i & 0xFF));

    assert(rt_binbuf_get_len(bb) == N);

    // Verify all written bytes are correct (no corruption from realloc)
    rt_binbuf_set_position(bb, 0);
    for (int i = 0; i < N; i++)
        assert(rt_binbuf_read_byte(bb) == (int64_t)(i & 0xFF));
}

static void test_large_single_write_grows_capacity()
{
    // A single write of 4 MB must grow the buffer past default capacity (256)
    const int64_t SIZE = 4 * 1024 * 1024;
    void *bb = rt_binbuf_new();

    void *src = rt_bytes_new(SIZE);
    for (int64_t i = 0; i < SIZE; i++)
        rt_bytes_set(src, i, (int64_t)(i & 0xFF));

    // Write length-prefixed bytes blob (exercises rt_binbuf_write_bytes memcpy path)
    rt_binbuf_write_bytes(bb, src);

    // Buffer should have grown to accommodate 4 + 4MB of data
    assert(rt_binbuf_get_len(bb) == 4 + SIZE);
}

//=============================================================================
// Multiple Values — Structured Protocol Simulation
//=============================================================================

static void test_structured_protocol_encode_decode()
{
    // Simulate a minimal binary frame: [version:byte][count:i32le][value:i64le]
    void *bb = rt_binbuf_new();
    rt_binbuf_write_byte(bb, 1);             // version
    rt_binbuf_write_i32le(bb, 42);           // count
    rt_binbuf_write_i64le(bb, 0xCAFEBABELL); // value

    assert(rt_binbuf_get_len(bb) == 1 + 4 + 8);

    rt_binbuf_set_position(bb, 0);
    assert(rt_binbuf_read_byte(bb) == 1);
    assert(rt_binbuf_read_i32le(bb) == 42);
    assert(rt_binbuf_read_i64le(bb) == (int64_t)0xCAFEBABELL);
}

//=============================================================================
// Main
//=============================================================================

int main()
{
    // Construction
    test_new_default();
    test_new_cap();
    test_new_cap_clamped();
    test_from_bytes();
    test_from_bytes_null();

    // Write / read round-trips
    test_write_read_byte();
    test_write_read_i16le();
    test_write_read_i16be();
    test_write_read_i32le();
    test_write_read_i32be();
    test_write_read_i64le();
    test_write_read_i64be();
    test_endian_byte_order_i16();
    test_write_read_str();
    test_write_read_bytes();

    // Cursor semantics
    test_position_advances_on_write();
    test_position_advances_on_read();
    test_set_position_clamps_to_len();
    test_read_past_end_traps();

    // to_bytes / from_bytes
    test_to_bytes_round_trip();
    test_from_bytes_to_bytes_identity();

    // Reset
    test_reset();

    // Capacity growth
    test_capacity_growth();
    test_large_single_write_grows_capacity();

    // Structured encoding
    test_structured_protocol_encode_decode();

    return 0;
}
