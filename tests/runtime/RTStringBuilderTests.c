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

// Override strlen within this translation unit to simulate an oversized
// formatting result. When the string builder under test points @ref g_overflow_target,
// the custom strlen reports a much larger length than actually written, forcing
// the overflow branch in rt_sb_append_double without invoking rt_trap.
static rt_string_builder *g_overflow_target = NULL;
static size_t g_forced_strlen_extra = 0;
static int g_forced_strlen_used = 0;

size_t strlen(const char *s)
{
    if (g_overflow_target)
    {
        const rt_string_builder *sb = g_overflow_target;
        if (s == sb->data + sb->len)
        {
            g_forced_strlen_used = 1;
            return sb->len + g_forced_strlen_extra;
        }
    }

    size_t len = 0;
    while (s[len] != '\0')
        ++len;
    return len;
}

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

static void test_append_double_overflow_preserves_state(void)
{
    rt_string_builder sb;
    rt_sb_init(&sb);

    assert(rt_sb_append_cstr(&sb, "prefix") == RT_SB_OK);

    char *original_data = sb.data;
    size_t original_len = sb.len;
    sb.cap = sb.len + 1;
    size_t original_cap = sb.cap;

    g_overflow_target = &sb;
    g_forced_strlen_extra = 1024;
    g_forced_strlen_used = 0;
    rt_sb_status status = rt_sb_append_double(&sb, 123.456);
    g_overflow_target = NULL;
    assert(g_forced_strlen_used);
    assert(status == RT_SB_ERROR_OVERFLOW);
    assert(sb.len == original_len);
    assert(sb.data == original_data);
    assert(sb.cap == original_cap);
    assert(sb.data[sb.len] == '\0');

    rt_sb_free(&sb);
}

int main(void)
{
    test_init_empty();
    test_tiny_append();
    test_large_append();
    test_printf_growth();
    test_numeric_helpers();
    test_append_double_overflow_preserves_state();
    return 0;
}
