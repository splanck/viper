//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTStringEdgeTests.cpp
// Purpose: Edge-case regression tests for the runtime string API. Covers
//          UTF-8 multi-byte handling, byte-indexing semantics, boundary
//          conditions in slicing/concat, and null terminator preservation.
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_string.h"

#include <cassert>
#include <cstring>

static rt_string make_str(const char *s)
{
    return rt_string_from_bytes(s, strlen(s));
}

static bool str_eq(rt_string s, const char *expected)
{
    if (!s && !expected)
        return true;
    if (!s || !expected)
        return false;
    return strcmp(rt_string_cstr(s), expected) == 0;
}

//===----------------------------------------------------------------------===//
// Empty string
//===----------------------------------------------------------------------===//

static void test_empty_string()
{
    rt_string empty = make_str("");
    assert(rt_str_len(empty) == 0);
    assert(rt_str_is_empty(empty));
    assert(rt_string_cstr(empty)[0] == '\0');
}

//===----------------------------------------------------------------------===//
// UTF-8 multi-byte: byte-length vs codepoint count
//===----------------------------------------------------------------------===//

static void test_utf8_byte_length()
{
    // "café" = 'c'(1) + 'a'(1) + 'f'(1) + 'é'(2) = 5 bytes, 4 codepoints
    rt_string cafe = make_str("caf\xc3\xa9");
    assert(rt_str_len(cafe) == 5);

    // "日本語" = 3 codepoints × 3 bytes = 9 bytes
    rt_string jp = make_str("\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e");
    assert(rt_str_len(jp) == 9);

    // 4-byte codepoint: U+1F600 (grinning face) = F0 9F 98 80
    rt_string emoji = make_str("\xf0\x9f\x98\x80");
    assert(rt_str_len(emoji) == 4);
}

//===----------------------------------------------------------------------===//
// rt_str_flip: codepoint-aware reversal
//===----------------------------------------------------------------------===//

static void test_flip_utf8()
{
    // "café" reversed by codepoints → "éfac"
    rt_string cafe = make_str("caf\xc3\xa9");
    rt_string flipped = rt_str_flip(cafe);
    assert(str_eq(flipped,
                  "\xc3\xa9"
                  "fac"));

    // "日本語" → "語本日"
    rt_string jp = make_str("\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e");
    rt_string jp_flipped = rt_str_flip(jp);
    assert(str_eq(jp_flipped, "\xe8\xaa\x9e\xe6\x9c\xac\xe6\x97\xa5"));
}

static void test_flip_empty()
{
    rt_string empty = make_str("");
    rt_string result = rt_str_flip(empty);
    assert(rt_str_len(result) == 0);
}

//===----------------------------------------------------------------------===//
// Byte-indexed slicing on multi-byte strings
//===----------------------------------------------------------------------===//

static void test_left_boundary()
{
    rt_string s = make_str("hello");
    // Left$(s, 0) → empty
    rt_string zero = rt_str_left(s, 0);
    assert(rt_str_len(zero) == 0);

    // Left$(s, len) → full string
    rt_string full = rt_str_left(s, 5);
    assert(str_eq(full, "hello"));
}

static void test_right_boundary()
{
    rt_string s = make_str("hello");
    // Right$(s, 0) → empty
    rt_string zero = rt_str_right(s, 0);
    assert(rt_str_len(zero) == 0);

    // Right$(s, len) → full string
    rt_string full = rt_str_right(s, 5);
    assert(str_eq(full, "hello"));
}

static void test_mid_boundary()
{
    rt_string s = make_str("hello");
    // Mid$ uses 1-based indexing (BASIC semantics).
    // Mid$(s, 6) on 5-byte string → empty (past end)
    rt_string at_end = rt_str_mid(s, 6);
    assert(rt_str_len(at_end) == 0);

    // Mid$(s, 1) → full string (start at beginning)
    rt_string from_start = rt_str_mid(s, 1);
    assert(str_eq(from_start, "hello"));

    // Mid$(s, 5) → last character
    rt_string last = rt_str_mid(s, 5);
    assert(str_eq(last, "o"));
}

static void test_substr_boundary()
{
    rt_string s = make_str("hello");
    // Substr at start, full length
    rt_string full = rt_str_substr(s, 0, 5);
    assert(str_eq(full, "hello"));

    // Substr at start, zero length
    rt_string zero = rt_str_substr(s, 0, 0);
    assert(rt_str_len(zero) == 0);
}

//===----------------------------------------------------------------------===//
// Slicing mid-codepoint (byte-indexed on multi-byte)
//===----------------------------------------------------------------------===//

static void test_slice_mid_codepoint()
{
    // "café" = c(0) a(1) f(2) é(3,4) — take Left$(s, 3) splits before é
    rt_string cafe = make_str("caf\xc3\xa9");
    rt_string left3 = rt_str_left(cafe, 3);
    assert(rt_str_len(left3) == 3);
    assert(str_eq(left3, "caf"));

    // Left$(s, 4) takes first byte of é, producing an ill-formed fragment
    rt_string left4 = rt_str_left(cafe, 4);
    assert(rt_str_len(left4) == 4);
    // The 4th byte is 0xC3 — first byte of the 2-byte é sequence
    const char *data = rt_string_cstr(left4);
    assert((unsigned char)data[3] == 0xC3);
}

//===----------------------------------------------------------------------===//
// Null terminator preservation through operations
//===----------------------------------------------------------------------===//

static void test_null_terminator_concat()
{
    rt_string a = make_str("hello");
    rt_string b = make_str(" world");
    // Retain both since concat may consume them
    rt_string_ref(a);
    rt_string_ref(b);
    rt_string result = rt_str_concat(a, b);
    const char *cstr = rt_string_cstr(result);
    assert(cstr[11] == '\0'); // Properly null-terminated
    assert(str_eq(result, "hello world"));
}

static void test_null_terminator_substr()
{
    rt_string s = make_str("hello world");
    rt_string sub = rt_str_substr(s, 0, 5);
    const char *cstr = rt_string_cstr(sub);
    assert(cstr[5] == '\0');
    assert(str_eq(sub, "hello"));
}

//===----------------------------------------------------------------------===//
// rt_string_from_bytes with explicit length (not strlen-based)
//===----------------------------------------------------------------------===//

static void test_from_bytes_explicit_length()
{
    // Create string from first 5 bytes of a longer buffer
    const char *buf = "hello world";
    rt_string s = rt_string_from_bytes(buf, 5);
    assert(rt_str_len(s) == 5);
    assert(str_eq(s, "hello"));
}

static void test_from_bytes_zero_length()
{
    rt_string s = rt_string_from_bytes("anything", 0);
    assert(rt_str_len(s) == 0);
}

//===----------------------------------------------------------------------===//
// ASCII case conversion with multi-byte pass-through
//===----------------------------------------------------------------------===//

static void test_ucase_ascii_only()
{
    // ASCII chars are uppercased; multi-byte UTF-8 passes through unchanged
    rt_string mixed = make_str("caf\xc3\xa9");
    rt_string upper = rt_str_ucase(mixed);
    assert(str_eq(upper, "CAF\xc3\xa9")); // é unchanged (multi-byte)
    assert(rt_str_len(upper) == 5);
}

static void test_lcase_ascii_only()
{
    rt_string s = make_str("HELLO");
    rt_string lower = rt_str_lcase(s);
    assert(str_eq(lower, "hello"));
}

//===----------------------------------------------------------------------===//
// Concat with empty strings
//===----------------------------------------------------------------------===//

static void test_concat_empty()
{
    rt_string a = make_str("hello");
    rt_string empty = make_str("");
    rt_string_ref(a);
    rt_string_ref(empty);
    rt_string result = rt_str_concat(a, empty);
    assert(str_eq(result, "hello"));
    assert(rt_str_len(result) == 5);
}

//===----------------------------------------------------------------------===//
// Main
//===----------------------------------------------------------------------===//

int main()
{
    test_empty_string();
    test_utf8_byte_length();
    test_flip_utf8();
    test_flip_empty();
    test_left_boundary();
    test_right_boundary();
    test_mid_boundary();
    test_substr_boundary();
    test_slice_mid_codepoint();
    test_null_terminator_concat();
    test_null_terminator_substr();
    test_from_bytes_explicit_length();
    test_from_bytes_zero_length();
    test_ucase_ascii_only();
    test_lcase_ascii_only();
    test_concat_empty();

    return 0;
}
