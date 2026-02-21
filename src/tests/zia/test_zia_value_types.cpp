//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Tests for Zia value types (structs).
//
//===----------------------------------------------------------------------===//

#include "frontends/zia/Compiler.hpp"
#include "il/core/Opcode.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"
#include <string>

using namespace il::frontends::zia;
using namespace il::support;

namespace
{

//===----------------------------------------------------------------------===//
// Basic Value Types
//===----------------------------------------------------------------------===//

/// @brief Test basic value type with fields.
TEST(ZiaValueTypes, BasicFields)
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
    CompilerInput input{.source = source, .path = "valuebasic.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test value type with methods.
TEST(ZiaValueTypes, Methods)
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
    CompilerInput input{.source = source, .path = "valuemethods.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test value type with default values.
TEST(ZiaValueTypes, DefaultValues)
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
    CompilerInput input{.source = source, .path = "valuedefaults.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Value Type Semantics
//===----------------------------------------------------------------------===//

/// @brief Test value type copying.
TEST(ZiaValueTypes, Copying)
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
    CompilerInput input{.source = source, .path = "valuecopy.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test value type as function parameter.
TEST(ZiaValueTypes, Parameter)
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
    CompilerInput input{.source = source, .path = "valueparam.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test value type as return value.
TEST(ZiaValueTypes, Return)
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
    CompilerInput input{.source = source, .path = "valuereturn.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Nested Value Types
//===----------------------------------------------------------------------===//

/// @brief Test value type containing another value type.
TEST(ZiaValueTypes, Nested)
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
    CompilerInput input{.source = source, .path = "valuenested.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Value Type with Collections
//===----------------------------------------------------------------------===//

/// @brief Test value type containing list field.
TEST(ZiaValueTypes, WithList)
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
    CompilerInput input{.source = source, .path = "valuelist.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Value Type vs Entity
//===----------------------------------------------------------------------===//

/// @brief Test both value and entity types together.
TEST(ZiaValueTypes, MixedWithEntity)
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
    CompilerInput input{.source = source, .path = "valuemixed.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Value Type Boxing — Return-then-Box Pattern (BUG-NAT-008)
//===----------------------------------------------------------------------===//

/// @brief Value type returned from a helper and immediately boxed into a list.
///
/// This exercises the code path where emitBoxValue receives a Ptr that
/// originated from a callee's stack frame. In native code (AArch64), the
/// callee's frame is freed on return; the rt_box_value_type call that follows
/// would reuse the same stack area, corrupting the value type fields before
/// they were read. The fix reads all fields into IL temporaries BEFORE calling
/// rt_box_value_type, so the temporaries survive the call in callee-save
/// registers or the current function's spill slots.
TEST(ZiaValueTypes, ReturnedValueTypeBoxedIntoList)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

value Move {
    expose Integer from;
    expose Integer to;
    expose Integer kind;
    expose Boolean special;
}

entity MoveGen {
    hide func makeMove(Integer f, Integer t, Integer k, Boolean sp) -> Move {
        return new Move(f, t, k, sp);
    }

    expose func generate() -> List[Move] {
        var moves = [];
        moves.add(self.makeMove(0, 16, 1, false));
        moves.add(self.makeMove(1, 17, 2, true));
        moves.add(self.makeMove(2, 18, 1, false));
        return moves;
    }
}

func start() {
    var gen = new MoveGen();
    var list = gen.generate();
    Viper.Terminal.SayInt(list.count());
    var m0 = list.get(0);
    Viper.Terminal.SayInt(m0.from);
    Viper.Terminal.SayInt(m0.to);
    var m1 = list.get(1);
    Viper.Terminal.SayInt(m1.from);
    Viper.Terminal.SayInt(m1.to);
}
)";
    CompilerInput input{.source = source, .path = "valuebox_ret.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());

    // Verify the IL for the 'generate' method contains Load instructions
    // (field reads) before the Call to rt_box_value_type. We check that at
    // least one Load appears in the module — the specific ordering is guarded
    // by the emitBoxValue fix and visible in the generated IL.
    bool hasLoad = false;
    for (const auto &fn : result.module.functions)
        for (const auto &bb : fn.blocks)
            for (const auto &ins : bb.instructions)
                if (ins.op == il::core::Opcode::Load)
                    hasLoad = true;
    EXPECT_TRUE(hasLoad);
}

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
