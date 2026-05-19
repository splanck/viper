//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTZiaCompletionStubTests.cpp
// Purpose: Verify the weak Zia completion bridge stubs report unavailable
//          interactive editor tooling while diagnostic checks stay quiet.
//
//===----------------------------------------------------------------------===//

#include "rt_string.h"

#include <cassert>
#include <cstring>

extern "C" {
rt_string rt_zia_complete(rt_string source, int64_t line, int64_t col);
rt_string rt_zia_complete_for_file(rt_string source, rt_string file_path, int64_t line, int64_t col);
rt_string rt_zia_signature_help(rt_string source, int64_t line, int64_t col);
rt_string rt_zia_signature_help_for_file(
    rt_string source, rt_string file_path, int64_t line, int64_t col);
rt_string rt_zia_check(rt_string source);
rt_string rt_zia_check_for_file(rt_string source, rt_string file_path);
rt_string rt_zia_hover(rt_string source, int64_t line, int64_t col);
rt_string rt_zia_hover_for_file(rt_string source, rt_string file_path, int64_t line, int64_t col);
rt_string rt_zia_symbols(rt_string source);
rt_string rt_zia_symbols_for_file(rt_string source, rt_string file_path);
void rt_zia_completion_clear_cache(void);
}

static void expect_contains(rt_string value, const char *needle) {
    assert(value != nullptr);
    const char *text = rt_string_cstr(value);
    assert(text != nullptr);
    assert(std::strstr(text, needle) != nullptr);
    rt_string_unref(value);
}

static void expect_empty(rt_string value) {
    assert(value != nullptr);
    assert(rt_str_len(value) == 0);
    const char *text = rt_string_cstr(value);
    assert(text != nullptr);
    assert(text[0] == '\0');
    rt_string_unref(value);
}

int main() {
    rt_string source = rt_string_from_bytes("module app;\n", 12);
    rt_string path = rt_string_from_bytes("app.zia", 7);

    expect_contains(rt_zia_complete(source, 1, 0), "Zia completion unavailable\t\t8\t");
    expect_contains(
        rt_zia_complete_for_file(source, path, 1, 0), "Zia completion unavailable\t\t8\t");
    expect_contains(rt_zia_signature_help(source, 1, 0), "link fe_zia");
    expect_contains(rt_zia_signature_help_for_file(source, path, 1, 0), "link fe_zia");
    expect_empty(rt_zia_check(source));
    expect_empty(rt_zia_check_for_file(source, path));
    expect_contains(rt_zia_hover(source, 1, 0), "link fe_zia");
    expect_contains(rt_zia_hover_for_file(source, path, 1, 0), "link fe_zia");
    expect_contains(rt_zia_symbols(source), "Zia completion unavailable\tstatus\t");
    expect_contains(rt_zia_symbols_for_file(source, path), "Zia completion unavailable\tstatus\t");

    rt_zia_completion_clear_cache();
    rt_string_unref(path);
    rt_string_unref(source);
    return 0;
}
