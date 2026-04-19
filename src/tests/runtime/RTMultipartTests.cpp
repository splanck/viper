//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTMultipartTests.cpp
// Purpose: Validate multipart form-data builder/parser hardening.
//
//===----------------------------------------------------------------------===//

#include "rt_bytes.h"
#include "rt_multipart.h"
#include "rt_string.h"

#include <cassert>
#include <cstdio>
#include <cstring>

static void test_result(const char *name, bool passed) {
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
    assert(passed);
}

static void test_builder_escapes_header_params() {
    printf("Testing Multipart builder header escaping:\n");

    void *mp = rt_multipart_new();
    void *file_bytes = rt_bytes_from_str(rt_const_cstr("payload"));
    rt_multipart_add_field(mp, rt_const_cstr("alpha\"\r\nX-Evil: 1"), rt_const_cstr("value"));
    rt_multipart_add_file(
        mp, rt_const_cstr("upload"), rt_const_cstr("evil\"\r\nX-Injected: 1.txt"), file_bytes);

    void *body = rt_multipart_build(mp);
    rt_string text = rt_bytes_to_str(body);
    const char *cstr = rt_string_cstr(text);

    test_result("Field name quote is escaped",
                strstr(cstr, "name=\"alpha\\\"  X-Evil: 1\"") != nullptr);
    test_result("Filename quote is escaped",
                strstr(cstr, "filename=\"evil\\\"  X-Injected: 1.txt\"") != nullptr);
    test_result("Field name did not inject a new header", strstr(cstr, "\r\nX-Evil: 1") == nullptr);
    test_result("Filename did not inject a new header",
                strstr(cstr, "\r\nX-Injected: 1.txt") == nullptr);

    rt_string_unref(text);
}

static void test_parser_handles_quoted_and_escaped_params() {
    printf("\nTesting Multipart parser quoted parameter handling:\n");

    const char *raw_body =
        "--abc123\r\n"
        "Content-Disposition: form-data; name=\"field\\\"name\"\r\n"
        "\r\n"
        "value\r\n"
        "--abc123\r\n"
        "Content-Disposition: form-data; name=\"file\\\"field\"; filename=\"a\\\"b.txt\"\r\n"
        "Content-Type: application/octet-stream\r\n"
        "\r\n"
        "xyz\r\n"
        "--abc123--\r\n";

    void *body = rt_bytes_from_str(rt_const_cstr(raw_body));
    void *mp =
        rt_multipart_parse(rt_const_cstr("multipart/form-data; charset=utf-8; BOUNDARY=\"abc123\""),
                           body);

    test_result("Multipart count matches", rt_multipart_count(mp) == 2);

    rt_string field = rt_multipart_get_field(mp, rt_const_cstr("field\"name"));
    test_result("Escaped quoted field name parsed",
                strcmp(rt_string_cstr(field), "value") == 0);
    rt_string_unref(field);

    void *file = rt_multipart_get_file(mp, rt_const_cstr("file\"field"));
    rt_string file_text = rt_bytes_to_str(file);
    test_result("Escaped quoted file field name parsed",
                strcmp(rt_string_cstr(file_text), "xyz") == 0);
    rt_string_unref(file_text);
}

static void test_parser_handles_bare_token_params() {
    printf("\nTesting Multipart parser bare token parameters:\n");

    const char *raw_body =
        "--plain\r\n"
        "Content-Disposition: form-data; name=plain\r\n"
        "\r\n"
        "ok\r\n"
        "--plain--\r\n";

    void *body = rt_bytes_from_str(rt_const_cstr(raw_body));
    void *mp = rt_multipart_parse(rt_const_cstr("multipart/form-data; boundary=plain"), body);

    test_result("Multipart count with bare token name matches", rt_multipart_count(mp) == 1);
    rt_string field = rt_multipart_get_field(mp, rt_const_cstr("plain"));
    test_result("Bare token field name parsed", strcmp(rt_string_cstr(field), "ok") == 0);
    rt_string_unref(field);
}

int main() {
    printf("=== RT Multipart Tests ===\n\n");

    test_builder_escapes_header_params();
    test_parser_handles_quoted_and_escaped_params();
    test_parser_handles_bare_token_params();

    printf("\nAll Multipart tests passed.\n");
    return 0;
}
