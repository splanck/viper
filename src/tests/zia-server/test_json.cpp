//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/zia-server/test_json.cpp
// Purpose: Unit tests for JSON value type, parser, and emitter.
// Key invariants:
//   - All JSON types parse correctly
//   - Round-trip: parse(emit(v)) == v
//   - Error cases throw std::runtime_error
// Ownership/Lifetime:
//   - Test-only file
// Links: tools/zia-server/Json.hpp
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"
#include "tools/zia-server/Json.hpp"

#include <stdexcept>
#include <string>

using namespace viper::server;

// ===== Constructors and Type =====

TEST(Json, NullDefault)
{
    JsonValue v;
    EXPECT_TRUE(v.isNull());
    EXPECT_EQ(v.type(), JsonType::Null);
}

TEST(Json, BoolTrue)
{
    JsonValue v(true);
    EXPECT_EQ(v.type(), JsonType::Bool);
    EXPECT_TRUE(v.asBool());
}

TEST(Json, BoolFalse)
{
    JsonValue v(false);
    EXPECT_EQ(v.type(), JsonType::Bool);
    EXPECT_FALSE(v.asBool());
}

TEST(Json, Int)
{
    JsonValue v(int64_t(42));
    EXPECT_EQ(v.type(), JsonType::Int);
    EXPECT_EQ(v.asInt(), 42);
}

TEST(Json, IntFromInt)
{
    JsonValue v(int(42));
    EXPECT_EQ(v.type(), JsonType::Int);
    EXPECT_EQ(v.asInt(), 42);
}

TEST(Json, NegativeInt)
{
    JsonValue v(int64_t(-99));
    EXPECT_EQ(v.asInt(), -99);
}

TEST(Json, Double)
{
    JsonValue v(3.14);
    EXPECT_EQ(v.type(), JsonType::Double);
    EXPECT_TRUE(v.asDouble() > 3.13 && v.asDouble() < 3.15);
}

TEST(Json, String)
{
    JsonValue v(std::string("hello"));
    EXPECT_EQ(v.type(), JsonType::String);
    EXPECT_EQ(v.asString(), "hello");
}

TEST(Json, StringView)
{
    JsonValue v(std::string_view("world"));
    EXPECT_EQ(v.asString(), "world");
}

TEST(Json, CString)
{
    JsonValue v("test");
    EXPECT_EQ(v.asString(), "test");
}

TEST(Json, EmptyArray)
{
    JsonValue v(JsonValue::ArrayType{});
    EXPECT_EQ(v.type(), JsonType::Array);
    EXPECT_EQ(v.size(), size_t(0));
}

TEST(Json, ArrayWithElements)
{
    auto v = JsonValue::array({JsonValue(1), JsonValue("two"), JsonValue(true)});
    EXPECT_EQ(v.size(), size_t(3));
    EXPECT_EQ(v.at(0).asInt(), 1);
    EXPECT_EQ(v.at(1).asString(), "two");
    EXPECT_TRUE(v.at(2).asBool());
}

TEST(Json, EmptyObject)
{
    JsonValue v(JsonValue::ObjectType{});
    EXPECT_EQ(v.type(), JsonType::Object);
    EXPECT_EQ(v.size(), size_t(0));
}

TEST(Json, ObjectWithMembers)
{
    auto v = JsonValue::object({
        {"name", JsonValue("viper")},
        {"version", JsonValue(1)},
    });
    EXPECT_EQ(v.size(), size_t(2));
    EXPECT_EQ(v["name"].asString(), "viper");
    EXPECT_EQ(v["version"].asInt(), 1);
    EXPECT_TRUE(v.has("name"));
    EXPECT_FALSE(v.has("missing"));
}

TEST(Json, ObjectGetNull)
{
    auto v = JsonValue::object({{"x", JsonValue(1)}});
    EXPECT_TRUE(v["missing"].isNull());
    EXPECT_TRUE(v.get("missing") == nullptr);
}

TEST(Json, ArrayOutOfRange)
{
    auto v = JsonValue::array({JsonValue(1)});
    EXPECT_TRUE(v.at(5).isNull());
}

// ===== Default values on type mismatch =====

TEST(Json, DefaultBool)
{
    JsonValue v(42);
    EXPECT_FALSE(v.asBool());    // default false
    EXPECT_TRUE(v.asBool(true)); // explicit default
}

TEST(Json, DefaultInt)
{
    JsonValue v("not a number");
    EXPECT_EQ(v.asInt(), 0);
    EXPECT_EQ(v.asInt(99), 99);
}

TEST(Json, IntFromDouble)
{
    JsonValue v(3.7);
    EXPECT_EQ(v.asInt(), 3); // truncation
}

TEST(Json, DoubleFromInt)
{
    JsonValue v(int64_t(42));
    EXPECT_TRUE(v.asDouble() > 41.9 && v.asDouble() < 42.1);
}

// ===== Emitter =====

TEST(Json, EmitNull)
{
    EXPECT_EQ(JsonValue().toCompactString(), "null");
}

TEST(Json, EmitBool)
{
    EXPECT_EQ(JsonValue(true).toCompactString(), "true");
    EXPECT_EQ(JsonValue(false).toCompactString(), "false");
}

TEST(Json, EmitInt)
{
    EXPECT_EQ(JsonValue(int64_t(42)).toCompactString(), "42");
    EXPECT_EQ(JsonValue(int64_t(-1)).toCompactString(), "-1");
    EXPECT_EQ(JsonValue(int64_t(0)).toCompactString(), "0");
}

TEST(Json, EmitString)
{
    EXPECT_EQ(JsonValue("hello").toCompactString(), "\"hello\"");
}

TEST(Json, EmitStringEscapes)
{
    EXPECT_EQ(JsonValue("a\"b").toCompactString(), "\"a\\\"b\"");
    EXPECT_EQ(JsonValue("a\\b").toCompactString(), "\"a\\\\b\"");
    EXPECT_EQ(JsonValue("a\nb").toCompactString(), "\"a\\nb\"");
    EXPECT_EQ(JsonValue("a\tb").toCompactString(), "\"a\\tb\"");
}

TEST(Json, EmitEmptyArray)
{
    EXPECT_EQ(JsonValue::array({}).toCompactString(), "[]");
}

TEST(Json, EmitArray)
{
    auto v = JsonValue::array({JsonValue(1), JsonValue("two")});
    EXPECT_EQ(v.toCompactString(), "[1,\"two\"]");
}

TEST(Json, EmitEmptyObject)
{
    EXPECT_EQ(JsonValue::object({}).toCompactString(), "{}");
}

TEST(Json, EmitObject)
{
    auto v = JsonValue::object({{"a", JsonValue(1)}, {"b", JsonValue("c")}});
    EXPECT_EQ(v.toCompactString(), "{\"a\":1,\"b\":\"c\"}");
}

// ===== Parser =====

TEST(Json, ParseNull)
{
    auto v = JsonValue::parse("null");
    EXPECT_TRUE(v.isNull());
}

TEST(Json, ParseTrue)
{
    auto v = JsonValue::parse("true");
    EXPECT_TRUE(v.asBool());
}

TEST(Json, ParseFalse)
{
    auto v = JsonValue::parse("false");
    EXPECT_FALSE(v.asBool());
}

TEST(Json, ParseInt)
{
    EXPECT_EQ(JsonValue::parse("42").asInt(), 42);
    EXPECT_EQ(JsonValue::parse("-7").asInt(), -7);
    EXPECT_EQ(JsonValue::parse("0").asInt(), 0);
}

TEST(Json, ParseDouble)
{
    auto v = JsonValue::parse("3.14");
    EXPECT_EQ(v.type(), JsonType::Double);
    EXPECT_TRUE(v.asDouble() > 3.13 && v.asDouble() < 3.15);
}

TEST(Json, ParseExponent)
{
    auto v = JsonValue::parse("1e10");
    EXPECT_EQ(v.type(), JsonType::Double);
    EXPECT_TRUE(v.asDouble() > 9e9);
}

TEST(Json, ParseNegativeExponent)
{
    auto v = JsonValue::parse("1.5e-3");
    EXPECT_TRUE(v.asDouble() > 0.0014 && v.asDouble() < 0.0016);
}

TEST(Json, ParseString)
{
    EXPECT_EQ(JsonValue::parse("\"hello\"").asString(), "hello");
}

TEST(Json, ParseStringEscapes)
{
    EXPECT_EQ(JsonValue::parse("\"a\\\"b\"").asString(), "a\"b");
    EXPECT_EQ(JsonValue::parse("\"a\\\\b\"").asString(), "a\\b");
    EXPECT_EQ(JsonValue::parse("\"a\\/b\"").asString(), "a/b");
    EXPECT_EQ(JsonValue::parse("\"a\\nb\"").asString(), "a\nb");
    EXPECT_EQ(JsonValue::parse("\"a\\tb\"").asString(), "a\tb");
    EXPECT_EQ(JsonValue::parse("\"a\\rb\"").asString(), "a\rb");
    EXPECT_EQ(JsonValue::parse("\"a\\bb\"").asString(), "a\bb");
    EXPECT_EQ(JsonValue::parse("\"a\\fb\"").asString(), "a\fb");
}

TEST(Json, ParseUnicodeEscape)
{
    // \u0041 = 'A'
    EXPECT_EQ(JsonValue::parse("\"\\u0041\"").asString(), "A");
}

TEST(Json, ParseEmptyArray)
{
    auto v = JsonValue::parse("[]");
    EXPECT_EQ(v.type(), JsonType::Array);
    EXPECT_EQ(v.size(), size_t(0));
}

TEST(Json, ParseArray)
{
    auto v = JsonValue::parse("[1, \"two\", true, null]");
    EXPECT_EQ(v.size(), size_t(4));
    EXPECT_EQ(v.at(0).asInt(), 1);
    EXPECT_EQ(v.at(1).asString(), "two");
    EXPECT_TRUE(v.at(2).asBool());
    EXPECT_TRUE(v.at(3).isNull());
}

TEST(Json, ParseNestedArray)
{
    auto v = JsonValue::parse("[[1,2],[3,4]]");
    EXPECT_EQ(v.size(), size_t(2));
    EXPECT_EQ(v.at(0).at(0).asInt(), 1);
    EXPECT_EQ(v.at(1).at(1).asInt(), 4);
}

TEST(Json, ParseEmptyObject)
{
    auto v = JsonValue::parse("{}");
    EXPECT_EQ(v.type(), JsonType::Object);
    EXPECT_EQ(v.size(), size_t(0));
}

TEST(Json, ParseObject)
{
    auto v = JsonValue::parse("{\"name\": \"viper\", \"version\": 1}");
    EXPECT_EQ(v["name"].asString(), "viper");
    EXPECT_EQ(v["version"].asInt(), 1);
}

TEST(Json, ParseNestedObject)
{
    auto v = JsonValue::parse("{\"a\": {\"b\": 42}}");
    EXPECT_EQ(v["a"]["b"].asInt(), 42);
}

TEST(Json, ParseWhitespace)
{
    auto v = JsonValue::parse("  \n\t { \"x\" : 1 } \n ");
    EXPECT_EQ(v["x"].asInt(), 1);
}

// ===== Round-trip =====

TEST(Json, RoundTripNull)
{
    auto v = JsonValue::parse(JsonValue().toCompactString());
    EXPECT_TRUE(v.isNull());
}

TEST(Json, RoundTripBool)
{
    EXPECT_TRUE(JsonValue::parse(JsonValue(true).toCompactString()).asBool());
    EXPECT_FALSE(JsonValue::parse(JsonValue(false).toCompactString()).asBool());
}

TEST(Json, RoundTripInt)
{
    EXPECT_EQ(JsonValue::parse(JsonValue(int64_t(12345)).toCompactString()).asInt(), 12345);
}

TEST(Json, RoundTripString)
{
    std::string original = "hello\n\"world\"\\tab\t";
    auto emitted = JsonValue(original).toCompactString();
    auto parsed = JsonValue::parse(emitted);
    EXPECT_EQ(parsed.asString(), original);
}

TEST(Json, RoundTripComplex)
{
    auto original = JsonValue::object({
        {"jsonrpc", JsonValue("2.0")},
        {"id", JsonValue(1)},
        {"result",
         JsonValue::object({
             {"tools",
              JsonValue::array({
                  JsonValue::object({
                      {"name", JsonValue("zia/check")},
                      {"description", JsonValue("Type-check a Zia file")},
                  }),
              })},
         })},
    });
    auto json = original.toCompactString();
    auto parsed = JsonValue::parse(json);
    EXPECT_EQ(parsed["jsonrpc"].asString(), "2.0");
    EXPECT_EQ(parsed["id"].asInt(), 1);
    EXPECT_EQ(parsed["result"]["tools"].at(0)["name"].asString(), "zia/check");
}

// ===== Parse errors =====

TEST(Json, ParseErrorEmpty)
{
    bool threw = false;
    try
    {
        JsonValue::parse("");
    }
    catch (const std::runtime_error &)
    {
        threw = true;
    }
    EXPECT_TRUE(threw);
}

TEST(Json, ParseErrorTrailingComma)
{
    bool threw = false;
    try
    {
        JsonValue::parse("[1,2,]");
    }
    catch (const std::runtime_error &)
    {
        threw = true;
    }
    EXPECT_TRUE(threw);
}

TEST(Json, ParseErrorTrailingGarbage)
{
    bool threw = false;
    try
    {
        JsonValue::parse("42 extra");
    }
    catch (const std::runtime_error &)
    {
        threw = true;
    }
    EXPECT_TRUE(threw);
}

TEST(Json, ParseErrorUnterminatedString)
{
    bool threw = false;
    try
    {
        JsonValue::parse("\"hello");
    }
    catch (const std::runtime_error &)
    {
        threw = true;
    }
    EXPECT_TRUE(threw);
}

TEST(Json, ParseErrorInvalidLiteral)
{
    bool threw = false;
    try
    {
        JsonValue::parse("tru");
    }
    catch (const std::runtime_error &)
    {
        threw = true;
    }
    EXPECT_TRUE(threw);
}

// ===== Equality =====

TEST(Json, EqualityNull)
{
    EXPECT_TRUE(JsonValue() == JsonValue());
}

TEST(Json, EqualityBool)
{
    EXPECT_TRUE(JsonValue(true) == JsonValue(true));
    EXPECT_FALSE(JsonValue(true) == JsonValue(false));
}

TEST(Json, EqualityInt)
{
    EXPECT_TRUE(JsonValue(int64_t(42)) == JsonValue(int64_t(42)));
    EXPECT_FALSE(JsonValue(int64_t(42)) == JsonValue(int64_t(43)));
}

TEST(Json, EqualityString)
{
    EXPECT_TRUE(JsonValue("a") == JsonValue("a"));
    EXPECT_FALSE(JsonValue("a") == JsonValue("b"));
}

TEST(Json, EqualityTypeMismatch)
{
    EXPECT_FALSE(JsonValue(int64_t(1)) == JsonValue(true));
    EXPECT_FALSE(JsonValue("1") == JsonValue(int64_t(1)));
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
