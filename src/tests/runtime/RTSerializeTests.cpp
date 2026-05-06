//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// RTSerializeTests.cpp - Tests for rt_serialize (unified serialization)
//===----------------------------------------------------------------------===//

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" {
#include "rt_internal.h"
#include "rt_json.h"
#include "rt_map.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_serialize.h"
#include "rt_string.h"

/// @brief Vm_trap.
void vm_trap(const char *msg) {
    fprintf(stderr, "TRAP: %s\n", msg);
    rt_abort(msg);
}
}

static int tests_run = 0;
static int tests_passed = 0;

#define ASSERT(cond, msg)                                                                          \
    do {                                                                                           \
        tests_run++;                                                                               \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg);                        \
        } else {                                                                                   \
            tests_passed++;                                                                        \
        }                                                                                          \
    } while (0)

static rt_string make_str(const char *s) {
    return rt_string_from_bytes(s, (int64_t)strlen(s));
}

//=============================================================================
// Format metadata tests
//=============================================================================

static void test_format_names() {
    rt_string name;

    name = rt_serialize_format_name(RT_FORMAT_JSON);
    ASSERT(strcmp(rt_string_cstr(name), "json") == 0, "JSON name");

    name = rt_serialize_format_name(RT_FORMAT_XML);
    ASSERT(strcmp(rt_string_cstr(name), "xml") == 0, "XML name");

    name = rt_serialize_format_name(RT_FORMAT_YAML);
    ASSERT(strcmp(rt_string_cstr(name), "yaml") == 0, "YAML name");

    name = rt_serialize_format_name(RT_FORMAT_TOML);
    ASSERT(strcmp(rt_string_cstr(name), "toml") == 0, "TOML name");

    name = rt_serialize_format_name(RT_FORMAT_CSV);
    ASSERT(strcmp(rt_string_cstr(name), "csv") == 0, "CSV name");

    name = rt_serialize_format_name(99);
    ASSERT(strcmp(rt_string_cstr(name), "unknown") == 0, "unknown format name");
}

static void test_mime_types() {
    rt_string mime;

    mime = rt_serialize_mime_type(RT_FORMAT_JSON);
    ASSERT(strcmp(rt_string_cstr(mime), "application/json") == 0, "JSON MIME");

    mime = rt_serialize_mime_type(RT_FORMAT_XML);
    ASSERT(strcmp(rt_string_cstr(mime), "application/xml") == 0, "XML MIME");

    mime = rt_serialize_mime_type(RT_FORMAT_CSV);
    ASSERT(strcmp(rt_string_cstr(mime), "text/csv") == 0, "CSV MIME");

    mime = rt_serialize_mime_type(RT_FORMAT_YAML);
    ASSERT(strcmp(rt_string_cstr(mime), "application/yaml") == 0, "YAML MIME");

    mime = rt_serialize_mime_type(RT_FORMAT_TOML);
    ASSERT(strcmp(rt_string_cstr(mime), "application/toml") == 0, "TOML MIME");
}

static void test_format_from_name() {
    ASSERT(rt_serialize_format_from_name(make_str("json")) == RT_FORMAT_JSON, "json -> JSON");
    ASSERT(rt_serialize_format_from_name(make_str("JSON")) == RT_FORMAT_JSON, "JSON -> JSON");
    ASSERT(rt_serialize_format_from_name(make_str("xml")) == RT_FORMAT_XML, "xml -> XML");
    ASSERT(rt_serialize_format_from_name(make_str("yaml")) == RT_FORMAT_YAML, "yaml -> YAML");
    ASSERT(rt_serialize_format_from_name(make_str("yml")) == RT_FORMAT_YAML, "yml -> YAML");
    ASSERT(rt_serialize_format_from_name(make_str("toml")) == RT_FORMAT_TOML, "toml -> TOML");
    ASSERT(rt_serialize_format_from_name(make_str("csv")) == RT_FORMAT_CSV, "csv -> CSV");
    ASSERT(rt_serialize_format_from_name(make_str("binary")) == -1, "binary -> unknown");
    ASSERT(rt_serialize_format_from_name(NULL) == -1, "null -> unknown");
}

//=============================================================================
// JSON round-trip
//=============================================================================

static void test_json_parse_format() {
    rt_string input = make_str("{\"name\":\"Alice\",\"age\":30}");
    void *parsed = rt_serialize_parse(input, RT_FORMAT_JSON);
    ASSERT(parsed != NULL, "JSON parsed");

    if (parsed) {
        rt_string output = rt_serialize_format(parsed, RT_FORMAT_JSON);
        ASSERT(output != NULL, "JSON formatted");
        ASSERT(rt_json_is_valid(output) == 1, "output is valid JSON");
    }
}

static void test_json_pretty() {
    rt_string input = make_str("{\"a\":1}");
    void *parsed = rt_serialize_parse(input, RT_FORMAT_JSON);
    if (parsed) {
        rt_string pretty = rt_serialize_format_pretty(parsed, RT_FORMAT_JSON, 2);
        ASSERT(pretty != NULL, "JSON pretty formatted");
        if (pretty)
            ASSERT(strchr(rt_string_cstr(pretty), '\n') != NULL, "pretty JSON has newlines");
    }
}

static void test_json_validate() {
    ASSERT(rt_serialize_is_valid(make_str("{\"a\":1}"), RT_FORMAT_JSON) == 1, "valid JSON");
    ASSERT(rt_serialize_is_valid(make_str("[1,2,3]"), RT_FORMAT_JSON) == 1, "valid JSON array");
    ASSERT(rt_serialize_is_valid(make_str("\"hello\""), RT_FORMAT_JSON) == 1, "valid JSON string");
}

//=============================================================================
// YAML round-trip
//=============================================================================

static void test_yaml_parse_format() {
    rt_string input = make_str("name: Alice\nage: 30\n");
    void *parsed = rt_serialize_parse(input, RT_FORMAT_YAML);
    ASSERT(parsed != NULL, "YAML parsed");

    if (parsed) {
        rt_string output = rt_serialize_format(parsed, RT_FORMAT_YAML);
        ASSERT(output != NULL, "YAML formatted");
        ASSERT(rt_str_len(output) > 0, "YAML output not empty");
    }
}

//=============================================================================
// XML round-trip
//=============================================================================

static void test_xml_parse_format() {
    rt_string input = make_str("<root><name>Alice</name><age>30</age></root>");
    void *parsed = rt_serialize_parse(input, RT_FORMAT_XML);
    ASSERT(parsed != NULL, "XML parsed");

    if (parsed) {
        rt_string output = rt_serialize_format(parsed, RT_FORMAT_XML);
        ASSERT(output != NULL, "XML formatted");
        if (output) {
            ASSERT(rt_serialize_is_valid(output, RT_FORMAT_XML) == 1, "formatted XML validates");
            ASSERT(strstr(rt_string_cstr(output), "<root>") != NULL, "formatted XML contains root");
        }
    }
}

static void test_xml_validate() {
    ASSERT(rt_serialize_is_valid(make_str("<a/>"), RT_FORMAT_XML) == 1, "valid XML");
}

//=============================================================================
// Auto-detection
//=============================================================================

static void test_detect_json() {
    ASSERT(rt_serialize_detect(make_str("{\"key\":\"value\"}")) == RT_FORMAT_JSON,
           "detect JSON obj");
    ASSERT(rt_serialize_detect(make_str("[1,2,3]")) == RT_FORMAT_JSON, "detect JSON arr");
}

static void test_detect_xml() {
    ASSERT(rt_serialize_detect(make_str("<root/>")) == RT_FORMAT_XML, "detect XML");
    ASSERT(rt_serialize_detect(make_str("<?xml version=\"1.0\"?>")) == RT_FORMAT_XML,
           "detect XML decl");
}

static void test_detect_yaml() {
    ASSERT(rt_serialize_detect(make_str("---\nkey: value")) == RT_FORMAT_YAML, "detect YAML ---");
    ASSERT(rt_serialize_detect(make_str("name: Alice")) == RT_FORMAT_YAML, "detect YAML colon");
}

static void test_detect_toml() {
    ASSERT(rt_serialize_detect(make_str("name = \"Alice\"")) == RT_FORMAT_TOML, "detect TOML kv");
    ASSERT(rt_serialize_detect(make_str("[server]\nname = \"Alice\"")) == RT_FORMAT_TOML,
           "detect TOML section");
    ASSERT(rt_serialize_detect(make_str("[1,2,3]")) == RT_FORMAT_JSON,
           "JSON arrays are not TOML sections");
}

static void test_detect_null() {
    ASSERT(rt_serialize_detect(NULL) == -1, "detect null = -1");
    ASSERT(rt_serialize_detect(make_str("")) == -1, "detect empty = -1");
    ASSERT(rt_serialize_detect(make_str("   \n\t  ")) == -1, "detect whitespace = -1");
    ASSERT(rt_serialize_detect(make_str("just words")) == -1, "plain text is unknown");
    ASSERT(rt_serialize_detect(make_str("name,age\nAlice,30\n")) == RT_FORMAT_CSV, "detect CSV");
}

static void test_auto_parse_json() {
    void *parsed = rt_serialize_auto_parse(make_str("{\"x\":1}"));
    ASSERT(parsed != NULL, "auto-parse JSON");
}

static void test_invalid_json_parse_reports_error() {
    void *parsed = rt_serialize_parse(make_str("{\"x\":"), RT_FORMAT_JSON);
    ASSERT(parsed == NULL, "invalid JSON parse returns NULL");
    rt_string err = rt_serialize_error();
    ASSERT(err != NULL && rt_str_len(err) > 0, "invalid JSON sets serialize error");
}

//=============================================================================
// Conversion
//=============================================================================

static void test_convert_json_to_yaml() {
    rt_string json_in = make_str("{\"name\":\"Alice\"}");
    rt_string yaml_out = rt_serialize_convert(json_in, RT_FORMAT_JSON, RT_FORMAT_YAML);
    ASSERT(yaml_out != NULL, "JSON->YAML conversion");
    ASSERT(rt_str_len(yaml_out) > 0, "YAML output not empty");
}

static void test_convert_json_to_json() {
    rt_string json_in = make_str("{\"a\":1}");
    rt_string json_out = rt_serialize_convert(json_in, RT_FORMAT_JSON, RT_FORMAT_JSON);
    ASSERT(json_out != NULL, "JSON->JSON conversion");
    ASSERT(rt_json_is_valid(json_out) == 1, "round-trip JSON valid");
}

static void test_convert_json_to_xml() {
    rt_string json_in = make_str("{\"name\":\"Alice\",\"age\":30}");
    rt_string xml_out = rt_serialize_convert(json_in, RT_FORMAT_JSON, RT_FORMAT_XML);
    ASSERT(xml_out != NULL, "JSON->XML conversion");
    ASSERT(rt_serialize_is_valid(xml_out, RT_FORMAT_XML) == 1, "JSON->XML output is valid XML");
    ASSERT(strstr(rt_string_cstr(xml_out), "<root>") != NULL, "JSON->XML output has root");
    ASSERT(strstr(rt_string_cstr(xml_out), "<name>Alice</name>") != NULL,
           "JSON->XML output preserves string field");
    ASSERT(strstr(rt_string_cstr(xml_out), "<age>30</age>") != NULL,
           "JSON->XML output preserves numeric field");
}

static void test_convert_xml_to_json() {
    rt_string xml_in = make_str("<root><item id=\"1\">one</item><item>two</item></root>");
    rt_string json_out = rt_serialize_convert(xml_in, RT_FORMAT_XML, RT_FORMAT_JSON);
    ASSERT(json_out != NULL, "XML->JSON conversion");
    ASSERT(rt_json_is_valid(json_out) == 1, "XML->JSON output is valid JSON");
    ASSERT(strstr(rt_string_cstr(json_out), "\"root\"") != NULL, "XML->JSON output has root key");
    ASSERT(strstr(rt_string_cstr(json_out), "\"item\"") != NULL, "XML->JSON output has child key");
    ASSERT(strstr(rt_string_cstr(json_out), "\"@attrs\"") != NULL,
           "XML->JSON output preserves attributes");
}

static void test_convert_json_to_toml_and_csv() {
    rt_string json_in = make_str("{\"name\":\"Alice\",\"age\":30}");
    rt_string toml_out = rt_serialize_convert(json_in, RT_FORMAT_JSON, RT_FORMAT_TOML);
    ASSERT(toml_out != NULL, "JSON->TOML conversion");
    ASSERT(rt_serialize_is_valid(toml_out, RT_FORMAT_TOML) == 1, "JSON->TOML output is valid TOML");

    rt_string csv_out = rt_serialize_convert(json_in, RT_FORMAT_JSON, RT_FORMAT_CSV);
    ASSERT(csv_out != NULL, "JSON->CSV conversion");
    ASSERT(strstr(rt_string_cstr(csv_out), "name") != NULL, "JSON->CSV output contains key");
    ASSERT(strstr(rt_string_cstr(csv_out), "Alice") != NULL, "JSON->CSV output contains value");
}

//=============================================================================
// Null safety
//=============================================================================

static void test_null_safety() {
    ASSERT(rt_serialize_is_valid(NULL, RT_FORMAT_JSON) == 0, "null is_valid = 0");
    ASSERT(rt_serialize_detect(NULL) == -1, "null detect = -1");
    ASSERT(rt_serialize_auto_parse(NULL) == NULL, "null auto = NULL");
    rt_string err = rt_serialize_error();
    ASSERT(err != NULL && rt_str_len(err) > 0, "null auto_parse sets error");

    rt_string result = rt_serialize_convert(NULL, RT_FORMAT_JSON, RT_FORMAT_YAML);
    ASSERT(result != NULL, "convert null returns string");
    ASSERT(rt_str_len(result) == 0, "convert null = empty");
}

static void test_error_reporting() {
    rt_string err = rt_serialize_error();
    ASSERT(err != NULL, "error returns string");

    // Unknown format
    rt_string bad = rt_serialize_format(NULL, 99);
    ASSERT(bad != NULL, "unknown format returns string");
    err = rt_serialize_error();
    ASSERT(rt_str_len(err) > 0, "error message set");
}

/// @brief Main.
int main() {
    // Metadata
    test_format_names();
    test_mime_types();
    test_format_from_name();

    // JSON
    test_json_parse_format();
    test_json_pretty();
    test_json_validate();

    // YAML
    test_yaml_parse_format();

    // XML
    test_xml_parse_format();
    test_xml_validate();

    // Detection
    test_detect_json();
    test_detect_xml();
    test_detect_yaml();
    test_detect_toml();
    test_detect_null();
    test_auto_parse_json();
    test_invalid_json_parse_reports_error();

    // Conversion
    test_convert_json_to_yaml();
    test_convert_json_to_json();
    test_convert_json_to_xml();
    test_convert_xml_to_json();
    test_convert_json_to_toml_and_csv();

    // Safety
    test_null_safety();
    test_error_reporting();

    printf("Serialization tests: %d/%d passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
