// File: tests/runtime/RTSplitFieldsTests.cpp
// Purpose: Ensure rt_split_fields tokenizes comma-separated input lines.
// Key invariants: Fields are trimmed, quotes removed, and extra fields counted.
// Ownership/Lifetime: Test releases all allocated runtime strings.
// Links: docs/codemap.md

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

    return 0;
}
