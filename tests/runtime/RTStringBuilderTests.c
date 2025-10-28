// File: tests/runtime/RTStringBuilderTests.c
// Purpose: Exercise the runtime string builder helper across edge sizes.
// Key invariants: Builder maintains null termination, grows without overflow, and
//                 keeps small strings in inline storage.
// Ownership/Lifetime: Each test initialises and frees its own builder instance.
// Links: docs/codemap.md

#include "rt_string_builder.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static void test_init_empty(void)
{
    rt_string_builder sb;
    rt_sb_init(&sb);
    assert(sb.len == 0);
    assert(sb.cap >= RT_SB_INLINE_CAPACITY);
    assert(sb.data == sb.inline_buffer);
    assert(sb.data[0] == '\0');
    rt_sb_free(&sb);
}

static void test_tiny_append(void)
{
    rt_string_builder sb;
    rt_sb_init(&sb);
    assert(rt_sb_append_cstr(&sb, "hi") == RT_SB_OK);
    assert(sb.len == 2);
    assert(strcmp(sb.data, "hi") == 0);
    assert(sb.data == sb.inline_buffer);
    rt_sb_free(&sb);
}

static void test_large_append(void)
{
    char buffer[512];
    memset(buffer, 'a', sizeof(buffer));
    buffer[sizeof(buffer) - 1] = '\0';

    rt_string_builder sb;
    rt_sb_init(&sb);
    rt_sb_status status = rt_sb_append_cstr(&sb, buffer);
    assert(status == RT_SB_OK);
    assert(sb.len == strlen(buffer));
    assert(strcmp(sb.data, buffer) == 0);
    assert(sb.data != sb.inline_buffer);
    rt_sb_free(&sb);
}

static void test_printf_growth(void)
{
    rt_string_builder sb;
    rt_sb_init(&sb);

    for (int i = 0; i < 64; ++i)
    {
        rt_sb_status status = rt_sb_printf(&sb, "line:%d;", i);
        assert(status == RT_SB_OK);
    }

    assert(sb.len > RT_SB_INLINE_CAPACITY);
    assert(strstr(sb.data, "line:63;") != NULL);
    rt_sb_free(&sb);
}

static void test_numeric_helpers(void)
{
    rt_string_builder sb;
    rt_sb_init(&sb);

    assert(rt_sb_append_int(&sb, -12345) == RT_SB_OK);
    assert(strcmp(sb.data, "-12345") == 0);

    assert(rt_sb_append_cstr(&sb, ",") == RT_SB_OK);
    assert(rt_sb_append_double(&sb, 3.5) == RT_SB_OK);
    assert(strstr(sb.data, "3.5") != NULL);

    rt_sb_free(&sb);
}

int main(void)
{
    test_init_empty();
    test_tiny_append();
    test_large_append();
    test_printf_growth();
    test_numeric_helpers();
    return 0;
}
