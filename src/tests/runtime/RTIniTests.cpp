//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTIniTests.cpp
// Purpose: Tests for Viper.Text.Ini runtime helpers.
//
//===----------------------------------------------------------------------===//

#include "rt_ini.h"
#include "rt_internal.h"
#include "rt_map.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <cassert>
#include <cstring>

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

static rt_string make_str(const char *s)
{
    return rt_string_from_bytes(s, strlen(s));
}

static bool str_eq(rt_string s, const char *expected)
{
    if (!s)
        return expected == nullptr || *expected == '\0';
    const char *cstr = rt_string_cstr(s);
    return cstr && strcmp(cstr, expected) == 0;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void test_parse_basic()
{
    rt_string input = make_str("[database]\n"
                               "host = localhost\n"
                               "port = 5432\n"
                               "\n"
                               "[app]\n"
                               "name = MyApp\n"
                               "debug = true\n");
    void *ini = rt_ini_parse(input);
    assert(ini != nullptr);

    rt_string sect = make_str("database");
    rt_string key_host = make_str("host");
    rt_string key_port = make_str("port");

    rt_string host = rt_ini_get(ini, sect, key_host);
    assert(str_eq(host, "localhost"));
    rt_string_unref(host);

    rt_string port = rt_ini_get(ini, sect, key_port);
    assert(str_eq(port, "5432"));
    rt_string_unref(port);

    rt_string_unref(sect);
    rt_string_unref(key_host);
    rt_string_unref(key_port);
    rt_string_unref(input);
}

static void test_parse_comments()
{
    rt_string input = make_str("; This is a comment\n"
                               "# This is also a comment\n"
                               "[section]\n"
                               "key = value\n");
    void *ini = rt_ini_parse(input);

    rt_string sect = make_str("section");
    rt_string key = make_str("key");
    rt_string val = rt_ini_get(ini, sect, key);
    assert(str_eq(val, "value"));

    rt_string_unref(val);
    rt_string_unref(sect);
    rt_string_unref(key);
    rt_string_unref(input);
}

static void test_parse_default_section()
{
    rt_string input = make_str("key1 = val1\n"
                               "key2 = val2\n"
                               "[named]\n"
                               "key3 = val3\n");
    void *ini = rt_ini_parse(input);

    rt_string empty = make_str("");
    rt_string k1 = make_str("key1");
    rt_string v1 = rt_ini_get(ini, empty, k1);
    assert(str_eq(v1, "val1"));

    rt_string_unref(v1);
    rt_string_unref(k1);
    rt_string_unref(empty);
    rt_string_unref(input);
}

static void test_parse_whitespace_trimming()
{
    rt_string input = make_str("[  section  ]\n"
                               "  key  =  value with spaces  \n");
    void *ini = rt_ini_parse(input);

    rt_string sect = make_str("section");
    rt_string key = make_str("key");
    rt_string val = rt_ini_get(ini, sect, key);
    assert(str_eq(val, "value with spaces"));

    rt_string_unref(val);
    rt_string_unref(sect);
    rt_string_unref(key);
    rt_string_unref(input);
}

static void test_has_section()
{
    rt_string input = make_str("[existing]\nfoo = bar\n");
    void *ini = rt_ini_parse(input);

    rt_string sect = make_str("existing");
    rt_string missing = make_str("missing");
    assert(rt_ini_has_section(ini, sect) == 1);
    assert(rt_ini_has_section(ini, missing) == 0);

    rt_string_unref(sect);
    rt_string_unref(missing);
    rt_string_unref(input);
}

static void test_sections_list()
{
    rt_string input = make_str("[alpha]\na = 1\n"
                               "[beta]\nb = 2\n");
    void *ini = rt_ini_parse(input);
    void *sects = rt_ini_sections(ini);
    // Should have 3: "", "alpha", "beta"
    assert(rt_seq_len(sects) == 3);

    rt_string_unref(input);
}

static void test_set_new_key()
{
    rt_string input = make_str("[s]\nk1 = v1\n");
    void *ini = rt_ini_parse(input);

    rt_string sect = make_str("s");
    rt_string k2 = make_str("k2");
    rt_string v2 = make_str("v2");
    rt_ini_set(ini, sect, k2, v2);

    rt_string got = rt_ini_get(ini, sect, k2);
    assert(str_eq(got, "v2"));

    rt_string_unref(got);
    rt_string_unref(sect);
    rt_string_unref(k2);
    rt_string_unref(v2);
    rt_string_unref(input);
}

static void test_set_creates_section()
{
    rt_string input = make_str("");
    void *ini = rt_ini_parse(input);

    rt_string sect = make_str("new_section");
    rt_string key = make_str("key");
    rt_string val = make_str("value");
    rt_ini_set(ini, sect, key, val);

    assert(rt_ini_has_section(ini, sect) == 1);
    rt_string got = rt_ini_get(ini, sect, key);
    assert(str_eq(got, "value"));

    rt_string_unref(got);
    rt_string_unref(sect);
    rt_string_unref(key);
    rt_string_unref(val);
    rt_string_unref(input);
}

static void test_remove()
{
    rt_string input = make_str("[s]\nk1 = v1\nk2 = v2\n");
    void *ini = rt_ini_parse(input);

    rt_string sect = make_str("s");
    rt_string k1 = make_str("k1");
    assert(rt_ini_remove(ini, sect, k1) == 1);

    rt_string got = rt_ini_get(ini, sect, k1);
    assert(str_eq(got, "")); // Should be empty after removal

    rt_string_unref(got);
    rt_string_unref(sect);
    rt_string_unref(k1);
    rt_string_unref(input);
}

static void test_remove_nonexistent()
{
    rt_string input = make_str("[s]\nk = v\n");
    void *ini = rt_ini_parse(input);

    rt_string sect = make_str("s");
    rt_string missing = make_str("missing");
    assert(rt_ini_remove(ini, sect, missing) == 0);

    rt_string_unref(sect);
    rt_string_unref(missing);
    rt_string_unref(input);
}

static void test_format()
{
    rt_string input = make_str("[server]\nhost = localhost\nport = 8080\n");
    void *ini = rt_ini_parse(input);
    rt_string formatted = rt_ini_format(ini);

    // The formatted output should contain the section and keys
    const char *f = rt_string_cstr(formatted);
    assert(f != nullptr);
    assert(strstr(f, "[server]") != nullptr);
    assert(strstr(f, "host = localhost") != nullptr);
    assert(strstr(f, "port = 8080") != nullptr);

    rt_string_unref(formatted);
    rt_string_unref(input);
}

static void test_get_missing_returns_empty()
{
    rt_string input = make_str("[s]\nk = v\n");
    void *ini = rt_ini_parse(input);

    rt_string sect = make_str("s");
    rt_string missing = make_str("missing");
    rt_string val = rt_ini_get(ini, sect, missing);
    assert(str_eq(val, ""));

    rt_string_unref(val);
    rt_string_unref(sect);
    rt_string_unref(missing);
    rt_string_unref(input);
}

static void test_null_safety()
{
    void *ini = rt_ini_parse(NULL);
    assert(ini != nullptr);       // Should return empty map
    assert(rt_map_len(ini) == 0); // No sections when input is NULL

    // Set/get should not crash on this empty map
    rt_string sect = make_str("s");
    rt_string key = make_str("k");
    rt_string val = make_str("v");
    rt_ini_set(ini, sect, key, val);
    rt_string got = rt_ini_get(ini, sect, key);
    assert(str_eq(got, "v"));

    rt_string_unref(got);
    rt_string_unref(sect);
    rt_string_unref(key);
    rt_string_unref(val);
}

static void test_crlf_line_endings()
{
    rt_string input = make_str("[s]\r\nk1 = v1\r\nk2 = v2\r\n");
    void *ini = rt_ini_parse(input);

    rt_string sect = make_str("s");
    rt_string k1 = make_str("k1");
    rt_string k2 = make_str("k2");

    rt_string v1 = rt_ini_get(ini, sect, k1);
    assert(str_eq(v1, "v1"));
    rt_string v2 = rt_ini_get(ini, sect, k2);
    assert(str_eq(v2, "v2"));

    rt_string_unref(v1);
    rt_string_unref(v2);
    rt_string_unref(sect);
    rt_string_unref(k1);
    rt_string_unref(k2);
    rt_string_unref(input);
}

int main()
{
    test_parse_basic();
    test_parse_comments();
    test_parse_default_section();
    test_parse_whitespace_trimming();
    test_has_section();
    test_sections_list();
    test_set_new_key();
    test_set_creates_section();
    test_remove();
    test_remove_nonexistent();
    test_format();
    test_get_missing_returns_empty();
    test_null_safety();
    test_crlf_line_endings();

    return 0;
}
