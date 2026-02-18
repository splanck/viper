//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTBufferOverflowTests.c
// Purpose: Regression tests for buffer overflow bugs in the Viper runtime.
//          Covers:
//            R-12 - rt_trie collect_keys fixed-buffer overflow for long keys
//            R-14 - rt_dateonly_format snprintf return-value clamping
//            R-21 - rt_pixels_resize OOB read for 1-pixel-wide/tall images
//
//===----------------------------------------------------------------------===//

#include "viper/runtime/rt.h"

#include "rt_dateonly.h"
#include "rt_pixels.h"
#include "rt_seq.h"
#include "rt_trie.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Helpers
//=============================================================================

/// Release an object allocated with rt_obj_new_i64.
static void release_obj(void *p)
{
    if (p && rt_obj_release_check0(p))
        rt_obj_free(p);
}

/// Create a small heap-allocated runtime object usable as a trie value.
static void *make_value(void)
{
    void *p = rt_obj_new_i64(0, 8);
    assert(p != NULL);
    return p;
}

/// Build an rt_string from a C string literal.
static rt_string make_key(const char *text)
{
    return rt_string_from_bytes(text, (int64_t)strlen(text));
}

/// Build an rt_string from a buffer of known length (may contain NULs).
static rt_string make_key_buf(const char *buf, size_t len)
{
    return rt_string_from_bytes(buf, (int64_t)len);
}

//=============================================================================
// R-12: rt_trie collect_keys — long-key overflow
//=============================================================================

/// Insert a single key of 4096 characters into a trie and verify rt_trie_keys
/// returns exactly one key.  Before the fix, collect_keys wrote past the end of
/// a 4096-byte stack buffer, causing undefined behaviour.
static void test_trie_single_long_key(void)
{
    const size_t KEY_LEN = 4096;

    char *long_key_buf = (char *)malloc(KEY_LEN);
    assert(long_key_buf != NULL);
    memset(long_key_buf, 'a', KEY_LEN);

    void *trie = rt_trie_new();
    assert(trie != NULL);

    rt_string key = make_key_buf(long_key_buf, KEY_LEN);
    void *val = make_value();
    rt_trie_put(trie, key, val);

    assert(rt_trie_len(trie) == 1);

    void *keys = rt_trie_keys(trie);
    assert(keys != NULL);
    assert(rt_seq_len(keys) == 1);

    release_obj(keys);
    rt_string_unref(key);
    release_obj(val);
    release_obj(trie);
    free(long_key_buf);
}

/// Insert 100 distinct keys each 4100 characters long, then call rt_trie_keys()
/// and verify all 100 keys are returned without crashing.
static void test_trie_many_long_keys(void)
{
    const size_t KEY_LEN = 4100;
    const int KEY_COUNT = 100;

    char *buf = (char *)malloc(KEY_LEN);
    assert(buf != NULL);

    void *trie = rt_trie_new();
    assert(trie != NULL);
    void *val = make_value();

    for (int i = 0; i < KEY_COUNT; i++)
    {
        // Make each key distinct by varying the first byte.
        memset(buf, 'b', KEY_LEN);
        // Use printable ASCII characters (33..126) for the first byte.
        buf[0] = (char)(33 + (i % 94));
        // Second byte differentiates runs that share the same first byte.
        buf[1] = (char)(33 + (i / 94));

        rt_string key = make_key_buf(buf, KEY_LEN);
        rt_trie_put(trie, key, val);
        rt_string_unref(key);
    }

    assert(rt_trie_len(trie) == (int64_t)KEY_COUNT);

    void *keys = rt_trie_keys(trie);
    assert(keys != NULL);
    assert(rt_seq_len(keys) == (int64_t)KEY_COUNT);

    release_obj(keys);
    release_obj(val);
    release_obj(trie);
    free(buf);
}

/// Verify rt_trie_with_prefix also handles long keys correctly.
static void test_trie_with_prefix_long_key(void)
{
    const size_t KEY_LEN = 4096;

    char *buf = (char *)malloc(KEY_LEN);
    assert(buf != NULL);
    memset(buf, 'c', KEY_LEN);

    void *trie = rt_trie_new();
    assert(trie != NULL);
    void *val = make_value();

    rt_string key = make_key_buf(buf, KEY_LEN);
    rt_trie_put(trie, key, val);
    rt_string_unref(key);

    // Query with the first 10 characters as a prefix.
    rt_string prefix = make_key_buf(buf, 10);
    void *results = rt_trie_with_prefix(trie, prefix);
    assert(results != NULL);
    assert(rt_seq_len(results) == 1);

    release_obj(results);
    rt_string_unref(prefix);
    release_obj(val);
    release_obj(trie);
    free(buf);
}

//=============================================================================
// R-14: rt_dateonly_format — snprintf return-value overflow
//=============================================================================

/// Format a date using a format string that repeatedly emits long tokens such
/// as full month names ("September" = 9 chars) and full day names
/// ("Wednesday" = 9 chars).  Repeating these across a 255-byte buffer boundary
/// triggered the overflow before the fix.  After the fix the output must be
/// null-terminated within the 256-byte buffer.
static void test_dateonly_format_long_output(void)
{
    // September 17, 2025 is a Wednesday.
    void *date = rt_dateonly_create(2025, 9, 17);
    assert(date != NULL);

    // Build a format string that produces ~18 chars per pair repeated many times.
    // "%B %A " -> "September Wednesday " = 19 chars, repeated 20 times = 380 chars.
    // The fixed buffer is 256 bytes, so this should be truncated safely.
    const char *fmt_cstr = "%B %A %B %A %B %A %B %A %B %A %B %A %B %A %B %A %B %A %B %A "
                           "%B %A %B %A %B %A %B %A %B %A %B %A %B %A %B %A %B %A %B %A ";
    rt_string fmt = make_key(fmt_cstr);
    rt_string result = rt_dateonly_format(date, fmt);

    // The result must not be empty and must be a valid (non-NULL) string.
    const char *cstr = rt_string_cstr(result);
    assert(cstr != NULL);

    // The output length must be <= 255 (buffer is 256 with NUL terminator).
    int64_t result_len = rt_str_len(result);
    assert(result_len >= 0);
    assert(result_len <= 255);

    rt_string_unref(fmt);
    rt_string_unref(result);
    release_obj(date);
}

/// Format a date with a format string that produces exactly 255 bytes to
/// verify the boundary condition is handled correctly.
static void test_dateonly_format_boundary(void)
{
    // Use January 1, 2000 — short month name "January" (7 chars), day is "Saturday" (8 chars).
    void *date = rt_dateonly_create(2000, 1, 1);
    assert(date != NULL);

    // Each "%B" emits "January" (7 chars) and each " " literal is 1 char.
    // 255 / 8 = 31 tokens -> produce ~248 chars; well under the limit.
    const char *fmt_cstr = "%Y-%m-%d %Y-%m-%d %Y-%m-%d %Y-%m-%d %Y-%m-%d %Y-%m-%d "
                           "%Y-%m-%d %Y-%m-%d %Y-%m-%d %Y-%m-%d %Y-%m-%d %Y-%m-%d";
    rt_string fmt = make_key(fmt_cstr);
    rt_string result = rt_dateonly_format(date, fmt);

    const char *cstr = rt_string_cstr(result);
    assert(cstr != NULL);

    int64_t result_len = rt_str_len(result);
    assert(result_len >= 0);
    assert(result_len <= 255);

    rt_string_unref(fmt);
    rt_string_unref(result);
    release_obj(date);
}

//=============================================================================
// R-21: rt_pixels_resize — OOB read for 1-pixel-wide/tall images
//=============================================================================

/// Resize a 1x1 pixel image to 10x10. Before the fix, bilinear interpolation
/// computed src_x = p->width - 2 = -1 which was then clamped to 0, but
/// the access p->data[... + src_x + 1] still used index 1 which is OOB.
static void test_pixels_resize_1x1(void)
{
    void *src = rt_pixels_new(1, 1);
    assert(src != NULL);

    // Set the single pixel to a known colour.
    rt_pixels_set(src, 0, 0, (int64_t)0xFF0000FF); // opaque red (0xAARRGGBB)

    void *dst = rt_pixels_resize(src, 10, 10);
    assert(dst != NULL);

    // Output dimensions must match the requested size.
    assert(rt_pixels_width(dst) == 10);
    assert(rt_pixels_height(dst) == 10);

    release_obj(dst);
    release_obj(src);
}

/// Resize a 1×100 pixel image (width=1, height=100) to verify the width=1
/// edge case is handled for a non-degenerate height.
static void test_pixels_resize_1xN(void)
{
    const int64_t H = 100;
    void *src = rt_pixels_new(1, H);
    assert(src != NULL);

    // Fill each row with a different colour.
    for (int64_t y = 0; y < H; y++)
        rt_pixels_set(src, 0, y, (int64_t)(0xFF000000 | (uint32_t)(y * 2)));

    void *dst = rt_pixels_resize(src, 8, 8);
    assert(dst != NULL);

    assert(rt_pixels_width(dst) == 8);
    assert(rt_pixels_height(dst) == 8);

    release_obj(dst);
    release_obj(src);
}

/// Resize a 100×1 pixel image (width=100, height=1) to exercise the height=1
/// edge case symmetrically.
static void test_pixels_resize_Nx1(void)
{
    const int64_t W = 100;
    void *src = rt_pixels_new(W, 1);
    assert(src != NULL);

    for (int64_t x = 0; x < W; x++)
        rt_pixels_set(src, x, 0, (int64_t)(0xFF000000 | (uint32_t)(x * 2)));

    void *dst = rt_pixels_resize(src, 8, 8);
    assert(dst != NULL);

    assert(rt_pixels_width(dst) == 8);
    assert(rt_pixels_height(dst) == 8);

    release_obj(dst);
    release_obj(src);
}

//=============================================================================
// Entry Point
//=============================================================================

int main(void)
{
    // R-12: trie long-key overflow
    test_trie_single_long_key();
    test_trie_many_long_keys();
    test_trie_with_prefix_long_key();

    // R-14: dateonly format snprintf clamping
    test_dateonly_format_long_output();
    test_dateonly_format_boundary();

    // R-21: pixels resize 1-pixel-wide/tall OOB read
    test_pixels_resize_1x1();
    test_pixels_resize_1xN();
    test_pixels_resize_Nx1();

    return 0;
}
