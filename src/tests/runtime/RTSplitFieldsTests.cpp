//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTSplitFieldsTests.cpp
// Purpose: Ensure rt_split_fields tokenizes comma-separated input lines.
// Key invariants: Fields are trimmed, quotes removed, and extra fields counted.
// Ownership/Lifetime: Test releases all allocated runtime strings.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"

#include <assert.h>
#include <string.h>

int main()
{
    const char *raw = "12, \"hi\" , \" spaced \"";
    rt_string line = rt_string_from_bytes(raw, (int64_t)strlen(raw));

    rt_string fields[3] = {0};
    int64_t count = rt_split_fields(line, fields, 3);
    assert(count == 3);
    assert(rt_to_int(fields[0]) == 12);
    const char *f1 = rt_string_cstr(fields[1]);
    assert(strcmp(f1, "hi") == 0);
    const char *f2 = rt_string_cstr(fields[2]);
    assert(strcmp(f2, " spaced ") == 0);

    int64_t zero_fields_count = rt_split_fields(line, NULL, 0);
    assert(zero_fields_count == 3);

    for (int i = 0; i < 3; ++i)
        rt_string_unref(fields[i]);
    rt_string_unref(line);

    const char *extraRaw = "1,2,3";
    rt_string extra = rt_string_from_bytes(extraRaw, (int64_t)strlen(extraRaw));
    rt_string limited[2] = {0};
    int64_t extraCount = rt_split_fields(extra, limited, 2);
    assert(extraCount == 3);
    assert(rt_to_int(limited[0]) == 1);
    assert(rt_to_int(limited[1]) == 2);

    for (int i = 0; i < 2; ++i)
        rt_string_unref(limited[i]);
    rt_string_unref(extra);

    const char *quoted = "\"Hello, world\",42, \"alpha, beta\"";
    rt_string quoted_line = rt_string_from_bytes(quoted, (int64_t)strlen(quoted));
    rt_string quoted_fields[3] = {0};
    int64_t quoted_count = rt_split_fields(quoted_line, quoted_fields, 3);
    assert(quoted_count == 3);
    const char *q0 = rt_string_cstr(quoted_fields[0]);
    assert(strcmp(q0, "Hello, world") == 0);
    assert(rt_to_int(quoted_fields[1]) == 42);
    const char *q2 = rt_string_cstr(quoted_fields[2]);
    assert(strcmp(q2, "alpha, beta") == 0);

    for (int i = 0; i < 3; ++i)
        rt_string_unref(quoted_fields[i]);
    rt_string_unref(quoted_line);

    const char *complex = "\"He said \"\"Hi, there\"\"\",99, \"Bare, field\"";
    rt_string complex_line = rt_string_from_bytes(complex, (int64_t)strlen(complex));
    rt_string complex_fields[3] = {0};
    int64_t complex_count = rt_split_fields(complex_line, complex_fields, 3);
    assert(complex_count == 3);
    const char *c0 = rt_string_cstr(complex_fields[0]);
    assert(strcmp(c0, "He said \"Hi, there\"") == 0);
    assert(rt_to_int(complex_fields[1]) == 99);
    const char *c2 = rt_string_cstr(complex_fields[2]);
    assert(strcmp(c2, "Bare, field") == 0);

    for (int i = 0; i < 3; ++i)
        rt_string_unref(complex_fields[i]);
    rt_string_unref(complex_line);

    const char *escaped = "\"Nested \"\"quotes\"\" stay\"";
    rt_string escaped_line = rt_string_from_bytes(escaped, (int64_t)strlen(escaped));
    rt_string escaped_fields[1] = {0};
    int64_t escaped_count = rt_split_fields(escaped_line, escaped_fields, 1);
    assert(escaped_count == 1);
    const char *escaped_value = rt_string_cstr(escaped_fields[0]);
    assert(strcmp(escaped_value, "Nested \"quotes\" stay") == 0);
    assert(strstr(escaped_value, "\"\"") == NULL);

    rt_string_unref(escaped_fields[0]);
    rt_string_unref(escaped_line);

    return 0;
}
