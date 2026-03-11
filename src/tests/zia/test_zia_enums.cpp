//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/zia/test_zia_enums.cpp
// Purpose: Unit tests for Zia enum declarations, type checking, and lowering.
// Key invariants:
//   - Enum declarations parse, analyze, and lower correctly
//   - Enum variants are distinct named integer constants
//   - Type safety: enums and integers are not interchangeable
//   - Variant access via dot notation (Color.Red)
// Ownership/Lifetime:
//   - Test-only file
// Links: frontends/zia/AST_Decl.hpp, frontends/zia/Types.hpp
//
//===----------------------------------------------------------------------===//

#include "frontends/zia/Compiler.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"
#include <string>

using namespace il::frontends::zia;
using namespace il::support;

namespace
{

// ===== Basic Declaration =====

TEST(ZiaEnums, BasicEnumDeclaration)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

enum Color {
    Red,
    Green,
    Blue,
}

func start() {
    var c: Color = Color.Red;
    Viper.Terminal.SayInt(0);
}
)";
    CompilerInput input{.source = source, .path = "enum.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());
}

TEST(ZiaEnums, ExplicitValues)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

enum HttpStatus {
    Ok = 200,
    NotFound = 404,
    ServerError = 500,
}

func start() {
    var s: HttpStatus = HttpStatus.NotFound;
    Viper.Terminal.SayInt(0);
}
)";
    CompilerInput input{.source = source, .path = "enum_explicit.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);
    EXPECT_TRUE(result.succeeded());
}

TEST(ZiaEnums, AutoIncrementAfterExplicit)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

enum Priority {
    Low,
    Medium = 5,
    High,
    Critical,
}

func start() {
    var p: Priority = Priority.High;
    Viper.Terminal.SayInt(0);
}
)";
    CompilerInput input{.source = source, .path = "enum_autoinc.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);
    EXPECT_TRUE(result.succeeded());
}

// ===== Variant Access =====

TEST(ZiaEnums, VariantAccessInExpression)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

enum Direction {
    North,
    South,
    East,
    West,
}

func start() {
    var d: Direction = Direction.East;
    if d == Direction.East {
        Viper.Terminal.SayInt(1);
    }
}
)";
    CompilerInput input{.source = source, .path = "enum_access.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);
    EXPECT_TRUE(result.succeeded());
}

TEST(ZiaEnums, EnumComparisonNotEqual)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

enum State {
    Idle,
    Running,
    Done,
}

func start() {
    var s: State = State.Running;
    if s != State.Done {
        Viper.Terminal.SayInt(1);
    }
}
)";
    CompilerInput input{.source = source, .path = "enum_neq.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);
    EXPECT_TRUE(result.succeeded());
}

// ===== Type Safety Errors =====

TEST(ZiaEnums, UnknownVariantError)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

enum Color {
    Red,
    Green,
    Blue,
}

func start() {
    var c: Color = Color.Yellow;
}
)";
    CompilerInput input{.source = source, .path = "enum_unknown.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaEnums, DuplicateVariantError)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

enum Color {
    Red,
    Green,
    Red,
}

func start() {
}
)";
    CompilerInput input{.source = source, .path = "enum_dup.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);
    EXPECT_FALSE(result.succeeded());
}

// ===== Enum as Function Parameter =====

TEST(ZiaEnums, EnumAsParameter)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

enum Color {
    Red,
    Green,
    Blue,
}

func describeColor(c: Color) -> Integer {
    if c == Color.Red {
        return 1;
    }
    return 0;
}

func start() {
    var result = describeColor(Color.Red);
    Viper.Terminal.SayInt(result);
}
)";
    CompilerInput input{.source = source, .path = "enum_param.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());
}

// ===== Enum as Return Type =====

TEST(ZiaEnums, EnumAsReturnType)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

enum Direction {
    North,
    South,
    East,
    West,
}

func defaultDirection() -> Direction {
    return Direction.North;
}

func start() {
    var d = defaultDirection();
    Viper.Terminal.SayInt(0);
}
)";
    CompilerInput input{.source = source, .path = "enum_return.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);
    EXPECT_TRUE(result.succeeded());
}

// ===== Negative Explicit Values =====

TEST(ZiaEnums, NegativeExplicitValues)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

enum Offset {
    Backward = -1,
    None = 0,
    Forward = 1,
}

func start() {
    var o: Offset = Offset.Backward;
    Viper.Terminal.SayInt(0);
}
)";
    CompilerInput input{.source = source, .path = "enum_neg.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);
    EXPECT_TRUE(result.succeeded());
}

// ===== Expose Enum =====

TEST(ZiaEnums, ExposeEnum)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

expose enum Visibility {
    Hidden,
    Visible,
}

func start() {
    var v: Visibility = Visibility.Visible;
    Viper.Terminal.SayInt(0);
}
)";
    CompilerInput input{.source = source, .path = "enum_expose.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);
    EXPECT_TRUE(result.succeeded());
}

// ===== Multiple Enums in Same Module =====

TEST(ZiaEnums, MultipleEnumsCoexist)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

enum Color {
    Red,
    Green,
    Blue,
}

enum Size {
    Small,
    Medium,
    Large,
}

func start() {
    var c: Color = Color.Green;
    var s: Size = Size.Large;
    Viper.Terminal.SayInt(0);
}
)";
    CompilerInput input{.source = source, .path = "enum_multi.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);
    EXPECT_TRUE(result.succeeded());
}

// ===== Match Pattern Integration =====

TEST(ZiaEnums, MatchExhaustiveEnum)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

enum Color {
    Red,
    Green,
    Blue,
}

func colorValue(c: Color) -> Integer {
    return match c {
        Color.Red => 1,
        Color.Green => 2,
        Color.Blue => 3,
    };
}

func start() {
    var result = colorValue(Color.Red);
    Viper.Terminal.SayInt(result);
}
)";
    CompilerInput input{.source = source, .path = "enum_match.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());
}

TEST(ZiaEnums, MatchEnumWithWildcard)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

enum Direction {
    North,
    South,
    East,
    West,
}

func isNorth(d: Direction) -> Integer {
    return match d {
        Direction.North => 1,
        _ => 0,
    };
}

func start() {
    var result = isNorth(Direction.North);
    Viper.Terminal.SayInt(result);
}
)";
    CompilerInput input{.source = source, .path = "enum_match_wild.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);
    EXPECT_TRUE(result.succeeded());
}

TEST(ZiaEnums, MatchEnumNonExhaustiveError)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

enum Color {
    Red,
    Green,
    Blue,
}

func colorValue(c: Color) -> Integer {
    return match c {
        Color.Red => 1,
        Color.Green => 2,
    };
}

func start() {
    Viper.Terminal.SayInt(0);
}
)";
    CompilerInput input{.source = source, .path = "enum_match_nonexh.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaEnums, MatchEnumStatement)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

enum State {
    Idle,
    Running,
    Done,
}

func start() {
    var s: State = State.Running;
    match s {
        State.Idle => Viper.Terminal.SayInt(0);
        State.Running => Viper.Terminal.SayInt(1);
        State.Done => Viper.Terminal.SayInt(2);
    }
}
)";
    CompilerInput input{.source = source, .path = "enum_match_stmt.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());
}

} // anonymous namespace

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
