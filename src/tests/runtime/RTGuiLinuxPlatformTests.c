//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTGuiLinuxPlatformTests.c
// Purpose: Headless regression coverage for Linux desktop-data parsers.
//
// Key invariants:
//   - XSettings accepts both protocol byte orders.
//   - Truncated, oversized, and invalid-byte-order records fail safely.
//
// Ownership/Lifetime:
//   - Uses stack-owned fixture bytes only.
//
// Links: src/runtime/graphics/gui/rt_gui_accessibility_linux.c
//
//===----------------------------------------------------------------------===//

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

extern int rt_gui_linux_test_parse_xsetting(const unsigned char *bytes,
                                            size_t length,
                                            const char *requested,
                                            int32_t *integer_out);

static void write_u16(unsigned char *out, uint16_t value, int big_endian) {
    out[big_endian ? 0 : 1] = (unsigned char)(value >> 8u);
    out[big_endian ? 1 : 0] = (unsigned char)value;
}

static void write_u32(unsigned char *out, uint32_t value, int big_endian) {
    for (int index = 0; index < 4; ++index) {
        int shift = big_endian ? 24 - index * 8 : index * 8;
        out[index] = (unsigned char)(value >> shift);
    }
}

static size_t make_integer_setting(unsigned char *bytes, int big_endian, int32_t value) {
    memset(bytes, 0, 32);
    bytes[0] = (unsigned char)(big_endian ? 1 : 0);
    write_u32(bytes + 8, 1, big_endian);
    bytes[12] = 0;
    write_u16(bytes + 14, 4, big_endian);
    memcpy(bytes + 16, "Test", 4);
    write_u32(bytes + 20, 7, big_endian);
    write_u32(bytes + 24, (uint32_t)value, big_endian);
    return 28;
}

int main(void) {
    unsigned char bytes[32];
    int32_t value = 0;
    size_t length = make_integer_setting(bytes, 0, 1234);
    assert(rt_gui_linux_test_parse_xsetting(bytes, length, "Test", &value));
    assert(value == 1234);

    length = make_integer_setting(bytes, 1, -55);
    assert(rt_gui_linux_test_parse_xsetting(bytes, length, "Test", &value));
    assert(value == -55);

    for (size_t truncated = 0; truncated < length; ++truncated)
        assert(!rt_gui_linux_test_parse_xsetting(bytes, truncated, "Test", &value));

    length = make_integer_setting(bytes, 0, 1);
    bytes[0] = 2;
    assert(!rt_gui_linux_test_parse_xsetting(bytes, length, "Test", &value));

    length = make_integer_setting(bytes, 0, 1);
    write_u16(bytes + 14, UINT16_MAX, 0);
    assert(!rt_gui_linux_test_parse_xsetting(bytes, length, "Test", &value));

    length = make_integer_setting(bytes, 0, 1);
    write_u16(bytes + 14, 4, 1);
    assert(!rt_gui_linux_test_parse_xsetting(bytes, length, "Test", &value));
    return 0;
}
