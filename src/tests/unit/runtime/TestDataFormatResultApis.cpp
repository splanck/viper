//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/runtime/TestDataFormatResultApis.cpp
// Purpose: Tests Result-returning text/data parsing APIs added by the runtime
//          public API overhaul.
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

extern "C" {
#include "rt_json_stream.h"
#include "rt_object.h"
#include "rt_result.h"
#include "rt_serialize.h"
#include "rt_string.h"
#include "rt_xml.h"
#include "rt_yaml.h"
}

/// @brief Release a runtime object test handle when its reference count reaches zero.
/// @param obj Runtime object handle, or NULL.
static void release_obj(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

/// @brief Assert that a Result is Err and carries a non-empty string message.
/// @param result Opaque Viper.Result object returned by a runtime API under test.
static void expect_err_with_message(void *result) {
    ASSERT_TRUE(result != nullptr);
    EXPECT_EQ(rt_result_is_err(result), 1);
    rt_string message = rt_result_unwrap_err_str(result);
    ASSERT_TRUE(message != nullptr);
    EXPECT_TRUE(rt_str_len(message) > 0);
}

TEST(DataFormatResultApis, XmlParseResultWrapsSuccessAndFailure) {
    void *ok = rt_xml_parse_result(rt_const_cstr("<root><item/></root>"));
    ASSERT_TRUE(ok != nullptr);
    EXPECT_EQ(rt_result_is_ok(ok), 1);
    void *doc = rt_result_unwrap(ok);
    ASSERT_TRUE(doc != nullptr);
    EXPECT_EQ(rt_xml_is_node(doc), 1);
    release_obj(ok);

    void *err = rt_xml_parse_result(rt_const_cstr("<root"));
    expect_err_with_message(err);
    release_obj(err);
}

TEST(DataFormatResultApis, YamlParseResultPreservesNullAsOk) {
    void *null_doc = rt_yaml_parse_result(rt_const_cstr("null"));
    ASSERT_TRUE(null_doc != nullptr);
    EXPECT_EQ(rt_result_is_ok(null_doc), 1);
    EXPECT_EQ(rt_result_unwrap(null_doc), nullptr);
    release_obj(null_doc);

    void *ok = rt_yaml_parse_result(rt_const_cstr("name: Alice\nage: 30\n"));
    ASSERT_TRUE(ok != nullptr);
    EXPECT_EQ(rt_result_is_ok(ok), 1);
    EXPECT_TRUE(rt_result_unwrap(ok) != nullptr);
    release_obj(ok);

    void *err = rt_yaml_parse_result(rt_const_cstr("name: [unterminated\n"));
    expect_err_with_message(err);
    release_obj(err);
}

TEST(DataFormatResultApis, SerializeParseResultWrapsFormatErrors) {
    void *ok = rt_serialize_parse_result(rt_const_cstr("{\"name\":\"Alice\"}"), RT_FORMAT_JSON);
    ASSERT_TRUE(ok != nullptr);
    EXPECT_EQ(rt_result_is_ok(ok), 1);
    EXPECT_TRUE(rt_result_unwrap(ok) != nullptr);
    release_obj(ok);

    void *bad_json = rt_serialize_parse_result(rt_const_cstr("{"), RT_FORMAT_JSON);
    expect_err_with_message(bad_json);
    release_obj(bad_json);

    void *bad_format = rt_serialize_parse_result(rt_const_cstr("{}"), 99);
    expect_err_with_message(bad_format);
    release_obj(bad_format);
}

TEST(DataFormatResultApis, SerializeAutoParseResultWrapsDetectionErrors) {
    void *ok = rt_serialize_auto_parse_result(rt_const_cstr("{\"kind\":\"json\"}"));
    ASSERT_TRUE(ok != nullptr);
    EXPECT_EQ(rt_result_is_ok(ok), 1);
    EXPECT_TRUE(rt_result_unwrap(ok) != nullptr);
    release_obj(ok);

    void *err = rt_serialize_auto_parse_result(rt_const_cstr("plain text"));
    expect_err_with_message(err);
    release_obj(err);
}

TEST(DataFormatResultApis, JsonStreamNextResultWrapsTokenErrors) {
    void *stream = rt_json_stream_new(rt_const_cstr("[1]"));
    ASSERT_TRUE(stream != nullptr);

    void *first = rt_json_stream_next_result(stream);
    ASSERT_TRUE(first != nullptr);
    EXPECT_EQ(rt_result_is_ok(first), 1);
    EXPECT_EQ(rt_result_unwrap_i64(first), RT_JSON_TOK_ARRAY_START);
    release_obj(first);
    release_obj(stream);

    void *bad_stream = rt_json_stream_new(rt_const_cstr("not json"));
    ASSERT_TRUE(bad_stream != nullptr);
    void *err = rt_json_stream_next_result(bad_stream);
    expect_err_with_message(err);
    release_obj(err);
    release_obj(bad_stream);
}

int main() {
    return viper_test::run_all_tests();
}
