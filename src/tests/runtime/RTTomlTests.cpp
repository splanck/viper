//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_toml.h"
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

static void test_parse_simple()
{
    rt_string src = make_str("title = \"TOML Test\"\nversion = \"1.0\"");
    void *root = rt_toml_parse(src);
    assert(root != NULL);
    assert(rt_map_len(root) == 2);

    rt_string k1 = make_str("title");
    rt_string k2 = make_str("version");
    void *v1 = rt_map_get(root, k1);
    void *v2 = rt_map_get(root, k2);
    assert(v1 != NULL);
    assert(v2 != NULL);
    assert(strcmp(rt_string_cstr((rt_string)v1), "TOML Test") == 0);
    assert(strcmp(rt_string_cstr((rt_string)v2), "1.0") == 0);

    rt_string_unref(k1);
    rt_string_unref(k2);
}

static void test_parse_section()
{
    rt_string src = make_str("[server]\nhost = \"localhost\"\nport = 8080\n");
    void *root = rt_toml_parse(src);
    assert(root != NULL);

    rt_string sk = make_str("server");
    void *section = rt_map_get(root, sk);
    assert(section != NULL);

    rt_string hk = make_str("host");
    void *host = rt_map_get(section, hk);
    assert(host != NULL);
    assert(strcmp(rt_string_cstr((rt_string)host), "localhost") == 0);

    rt_string_unref(sk);
    rt_string_unref(hk);
}

static void test_parse_comments()
{
    rt_string src = make_str("# This is a comment\nkey = \"value\"\n# Another comment\n");
    void *root = rt_toml_parse(src);
    assert(root != NULL);
    assert(rt_map_len(root) == 1);
}

static void test_parse_quoted_values()
{
    rt_string src = make_str("name = \"hello world\"\npath = 'C:\\Users\\test'\n");
    void *root = rt_toml_parse(src);
    assert(root != NULL);

    rt_string nk = make_str("name");
    void *name = rt_map_get(root, nk);
    assert(strcmp(rt_string_cstr((rt_string)name), "hello world") == 0);

    rt_string_unref(nk);
}

static void test_parse_bare_values()
{
    rt_string src = make_str("count = 42\nenabled = true\n");
    void *root = rt_toml_parse(src);
    assert(root != NULL);

    rt_string ck = make_str("count");
    void *count = rt_map_get(root, ck);
    assert(strcmp(rt_string_cstr((rt_string)count), "42") == 0);

    rt_string ek = make_str("enabled");
    void *enabled = rt_map_get(root, ek);
    assert(strcmp(rt_string_cstr((rt_string)enabled), "true") == 0);

    rt_string_unref(ck);
    rt_string_unref(ek);
}

static void test_is_valid()
{
    rt_string valid = make_str("key = \"value\"\n");
    assert(rt_toml_is_valid(valid) == 1);
}

static void test_get_dotted()
{
    rt_string src = make_str("[database]\nhost = \"db.example.com\"\nport = 5432\n");
    void *root = rt_toml_parse(src);

    rt_string path = make_str("database.host");
    void *val = rt_toml_get(root, path);
    assert(val != NULL);
    assert(strcmp(rt_string_cstr((rt_string)val), "db.example.com") == 0);

    rt_string_unref(path);
}

static void test_null_safety()
{
    assert(rt_toml_parse(NULL) == NULL);
    assert(rt_toml_is_valid(NULL) == 0);
    assert(rt_toml_get(NULL, NULL) == NULL);
}

static void test_empty()
{
    rt_string src = make_str("");
    void *root = rt_toml_parse(src);
    assert(root != NULL);
    assert(rt_map_len(root) == 0);
}

int main()
{
    test_parse_simple();
    test_parse_section();
    test_parse_comments();
    test_parse_quoted_values();
    test_parse_bare_values();
    test_is_valid();
    test_get_dotted();
    test_null_safety();
    test_empty();
    return 0;
}
