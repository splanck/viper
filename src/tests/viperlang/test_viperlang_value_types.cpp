//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Tests for ViperLang value types (structs).
//
//===----------------------------------------------------------------------===//

#include "frontends/viperlang/Compiler.hpp"
#include "il/core/Opcode.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"
#include <string>

using namespace il::frontends::viperlang;
using namespace il::support;

namespace
{

//===----------------------------------------------------------------------===//
// Basic Value Types
//===----------------------------------------------------------------------===//

/// @brief Test basic value type with fields.
TEST(ViperLangValueTypes, BasicFields)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

value Point {
    expose Integer x;
    expose Integer y;
}

func start() {
    var p: Point;
    p.x = 10;
    p.y = 20;
    Viper.Terminal.SayInt(p.x);
    Viper.Terminal.SayInt(p.y);
}
)";
    CompilerInput input{.source = source, .path = "valuebasic.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test value type with methods.
TEST(ViperLangValueTypes, Methods)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

value Vector2D {
    expose Integer x;
    expose Integer y;

    expose func lengthSquared() -> Integer {
        return x * x + y * y;
    }

    expose func add(Vector2D other) -> Vector2D {
        var result: Vector2D;
        result.x = x + other.x;
        result.y = y + other.y;
        return result;
    }
}

func start() {
    var v1: Vector2D;
    v1.x = 3;
    v1.y = 4;
    Viper.Terminal.SayInt(v1.lengthSquared());
}
)";
    CompilerInput input{.source = source, .path = "valuemethods.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test value type with default values.
TEST(ViperLangValueTypes, DefaultValues)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

value Config {
    expose Integer width = 800;
    expose Integer height = 600;
    expose Boolean fullscreen = false;
}

func start() {
    var config: Config;
    Viper.Terminal.SayInt(config.width);
    Viper.Terminal.SayInt(config.height);
    Viper.Terminal.SayBool(config.fullscreen);
}
)";
    CompilerInput input{.source = source, .path = "valuedefaults.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Value Type Semantics
//===----------------------------------------------------------------------===//

/// @brief Test value type copying.
TEST(ViperLangValueTypes, Copying)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

value Point {
    expose Integer x;
    expose Integer y;
}

func start() {
    var p1: Point;
    p1.x = 10;
    p1.y = 20;

    var p2 = p1;  // Copy
    p2.x = 100;   // Modify copy

    // Original should be unchanged
    Viper.Terminal.SayInt(p1.x);  // 10
    Viper.Terminal.SayInt(p2.x);  // 100
}
)";
    CompilerInput input{.source = source, .path = "valuecopy.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test value type as function parameter.
TEST(ViperLangValueTypes, Parameter)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

value Point {
    expose Integer x;
    expose Integer y;
}

func printPoint(Point p) {
    Viper.Terminal.SayInt(p.x);
    Viper.Terminal.SayInt(p.y);
}

func start() {
    var p: Point;
    p.x = 5;
    p.y = 10;
    printPoint(p);
}
)";
    CompilerInput input{.source = source, .path = "valueparam.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test value type as return value.
TEST(ViperLangValueTypes, Return)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

value Point {
    expose Integer x;
    expose Integer y;
}

func createPoint(Integer x, Integer y) -> Point {
    var p: Point;
    p.x = x;
    p.y = y;
    return p;
}

func start() {
    var p = createPoint(15, 25);
    Viper.Terminal.SayInt(p.x);
    Viper.Terminal.SayInt(p.y);
}
)";
    CompilerInput input{.source = source, .path = "valuereturn.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Nested Value Types
//===----------------------------------------------------------------------===//

/// @brief Test value type containing another value type.
TEST(ViperLangValueTypes, Nested)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

value Point {
    expose Integer x;
    expose Integer y;
}

value Rectangle {
    expose Point topLeft;
    expose Point bottomRight;

    expose func width() -> Integer {
        return bottomRight.x - topLeft.x;
    }

    expose func height() -> Integer {
        return bottomRight.y - topLeft.y;
    }
}

func start() {
    var rect: Rectangle;
    rect.topLeft.x = 0;
    rect.topLeft.y = 0;
    rect.bottomRight.x = 100;
    rect.bottomRight.y = 50;

    Viper.Terminal.SayInt(rect.width());
    Viper.Terminal.SayInt(rect.height());
}
)";
    CompilerInput input{.source = source, .path = "valuenested.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Value Type with Collections
//===----------------------------------------------------------------------===//

/// @brief Test value type containing list field.
TEST(ViperLangValueTypes, WithList)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

value Polygon {
    expose List[Integer] xCoords;
    expose List[Integer] yCoords;

    expose func vertexCount() -> Integer {
        return xCoords.count();
    }
}

func start() {
    var poly: Polygon;
    poly.xCoords = [];
    poly.yCoords = [];
    poly.xCoords.add(0);
    poly.xCoords.add(10);
    poly.xCoords.add(5);
    poly.yCoords.add(0);
    poly.yCoords.add(0);
    poly.yCoords.add(10);

    Viper.Terminal.SayInt(poly.vertexCount());
}
)";
    CompilerInput input{.source = source, .path = "valuelist.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Value Type vs Entity
//===----------------------------------------------------------------------===//

/// @brief Test both value and entity types together.
TEST(ViperLangValueTypes, MixedWithEntity)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

value Position {
    expose Integer x;
    expose Integer y;
}

entity Player {
    expose String name;
    expose Position pos;
    expose Integer health;

    expose func moveTo(Integer x, Integer y) {
        pos.x = x;
        pos.y = y;
    }
}

func start() {
    var player = new Player();
    player.name = "Hero";
    player.pos.x = 0;
    player.pos.y = 0;
    player.health = 100;

    player.moveTo(10, 20);

    Viper.Terminal.Say(player.name);
    Viper.Terminal.SayInt(player.pos.x);
    Viper.Terminal.SayInt(player.pos.y);
}
)";
    CompilerInput input{.source = source, .path = "valuemixed.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
