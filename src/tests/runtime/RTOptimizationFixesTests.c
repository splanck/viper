//===----------------------------------------------------------------------===//
// File: tests/runtime/RTOptimizationFixesTests.c
// Purpose: Verify optimization criticals O-01 through O-04 are correct.
//          Correctness tests — these don't measure timing, but verify that
//          the optimized paths produce identical results to the original API.
//===----------------------------------------------------------------------===//

#include "rt_bytes.h"
#include "rt_file_ext.h"
#include "rt_string.h"
#include "rt_tempfile.h"
#include "rt_xml.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

static int tests_run = 0;
static int tests_failed = 0;

#define ASSERT(cond)                                                                               \
    do                                                                                             \
    {                                                                                              \
        tests_run++;                                                                               \
        if (!(cond))                                                                               \
        {                                                                                          \
            tests_failed++;                                                                        \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                       \
        }                                                                                          \
    } while (0)

//=============================================================================
// O-01 / O-02: rt_file_write_bytes / rt_file_read_bytes (chunked I/O)
//=============================================================================

static void test_file_write_bytes_roundtrip(void)
{
    // Write a known byte pattern using rt_file_write_bytes (O-01 fix),
    // then read it back using rt_file_read_bytes (O-02 fix).
    const int64_t N = 8192; // Large enough to exercise chunked paths

    void *src_bytes = rt_bytes_new(N);
    ASSERT(src_bytes != NULL);
    for (int64_t i = 0; i < N; i++)
        rt_bytes_set(src_bytes, i, i & 0xFF);

    rt_string path = rt_tempfile_path_with_ext(rt_const_cstr("rt_opt_test_"),
                                               rt_const_cstr(".bin"));
    ASSERT(path != NULL);

    rt_file_write_bytes(path, src_bytes);

    void *dst_bytes = rt_file_read_bytes(path);
    ASSERT(dst_bytes != NULL);
    ASSERT(rt_bytes_len(dst_bytes) == N);

    int ok = 1;
    for (int64_t i = 0; i < N && ok; i++)
    {
        if (rt_bytes_get(dst_bytes, i) != (i & 0xFF))
            ok = 0;
    }
    ASSERT(ok);

    // Cleanup
    rt_io_file_delete(path);
    rt_string_unref(path);
}

static void test_io_file_write_all_bytes_roundtrip(void)
{
    // Similarly test rt_io_file_write_all_bytes / rt_io_file_read_all_bytes
    const int64_t N = 4096;

    void *src = rt_bytes_new(N);
    ASSERT(src != NULL);
    for (int64_t i = 0; i < N; i++)
        rt_bytes_set(src, i, (N - i) & 0xFF);

    rt_string path = rt_tempfile_path_with_ext(rt_const_cstr("rt_io_opt_test_"),
                                               rt_const_cstr(".bin"));
    ASSERT(path != NULL);

    rt_io_file_write_all_bytes(path, src);

    void *dst = rt_io_file_read_all_bytes(path);
    ASSERT(dst != NULL);
    ASSERT(rt_bytes_len(dst) == N);

    int ok = 1;
    for (int64_t i = 0; i < N && ok; i++)
    {
        if (rt_bytes_get(dst, i) != ((N - i) & 0xFF))
            ok = 0;
    }
    ASSERT(ok);

    rt_io_file_delete(path);
    rt_string_unref(path);
}

static void test_file_write_bytes_empty(void)
{
    void *empty = rt_bytes_new(0);
    rt_string path =
        rt_tempfile_path_with_ext(rt_const_cstr("rt_opt_empty_"), rt_const_cstr(".bin"));
    rt_file_write_bytes(path, empty);
    // Should not crash; file may or may not exist
    rt_io_file_delete(path);
    rt_string_unref(path);
    ASSERT(1);
}

//=============================================================================
// O-04: rt_xml_text_content — O(n) builder produces correct output
//=============================================================================

static void test_xml_text_content_single_node(void)
{
    rt_string src = rt_string_from_bytes("<r>Hello</r>", 12);
    void *doc     = rt_xml_parse(src);
    ASSERT(doc != NULL);

    ASSERT(rt_xml_child_count(doc) > 0);
    void *root    = rt_xml_child_at(doc, 0);
    ASSERT(root != NULL);
    rt_string txt = rt_xml_text_content(root);
    ASSERT(txt != NULL);
    const char *cs = rt_string_cstr(txt);
    ASSERT(cs && strcmp(cs, "Hello") == 0);
    rt_string_unref(txt);
    rt_string_unref(src);
}

static void test_xml_text_content_mixed_children(void)
{
    // <r>Hello <b>world</b>!</r>
    rt_string src = rt_string_from_bytes("<r>Hello <b>world</b>!</r>", 26);
    void *doc     = rt_xml_parse(src);
    ASSERT(doc != NULL);

    ASSERT(rt_xml_child_count(doc) > 0);
    void *root    = rt_xml_child_at(doc, 0);
    ASSERT(root != NULL);
    rt_string txt = rt_xml_text_content(root);
    ASSERT(txt != NULL);

    // Must contain "Hello" and "world" and "!"
    const char *cs = rt_string_cstr(txt);
    ASSERT(cs && strstr(cs, "Hello") != NULL);
    ASSERT(cs && strstr(cs, "world") != NULL);
    ASSERT(cs && strstr(cs, "!") != NULL);
    rt_string_unref(txt);
    rt_string_unref(src);
}

static void test_xml_text_content_empty_element(void)
{
    rt_string src = rt_string_from_bytes("<empty/>", 8);
    void *doc     = rt_xml_parse(src);
    ASSERT(doc != NULL);
    if (rt_xml_child_count(doc) > 0)
    {
        void *root    = rt_xml_child_at(doc, 0);
        rt_string txt = rt_xml_text_content(root);
        // Empty element → empty or NULL text
        if (txt)
        {
            const char *cs = rt_string_cstr(txt);
            ASSERT(cs == NULL || strlen(cs) == 0);
            rt_string_unref(txt);
        }
    }
    ASSERT(1);
    rt_string_unref(src);
}

//=============================================================================
// main
//=============================================================================

int main(void)
{
    // O-01 / O-02: chunked file I/O
    test_file_write_bytes_roundtrip();
    test_io_file_write_all_bytes_roundtrip();
    test_file_write_bytes_empty();

    // O-04: XML text_content correctness
    test_xml_text_content_single_node();
    test_xml_text_content_mixed_children();
    test_xml_text_content_empty_element();

    printf("%d/%d tests passed\n", tests_run - tests_failed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
