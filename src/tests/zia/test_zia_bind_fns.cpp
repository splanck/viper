//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Tests for Zia 'bind' resolution of standalone runtime functions.
// Fixes bugs A-002, A-003, A-004, A-005.
//
//===----------------------------------------------------------------------===//

#include "frontends/zia/Compiler.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"

using namespace il::frontends::zia;
using namespace il::support;

namespace
{

bool compileOk(const std::string &source)
{
    SourceManager sm;
    CompilerInput input{.source = source, .path = "<test>"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);
    if (!result.succeeded())
    {
        std::cerr << "Compilation failed:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }
    return result.succeeded();
}

// A-002: bind Viper.Core.Box — standalone functions now importable
TEST(ZiaBindFns, BoxFunctions)
{
    EXPECT_TRUE(compileOk(R"(
module TestBox;
bind Viper.Core.Box;
func start() {
    var b = I64(42);
    var v = ToI64(b);
}
)"));
}

// A-003: bind Viper.Core.Parse — standalone functions now importable
TEST(ZiaBindFns, ParseFunctions)
{
    EXPECT_TRUE(compileOk(R"(
module TestParse;
bind Viper.Core.Parse;
func start() {
    var x = IntOr("42", 0);
    var y = NumOr("3.14", 0.0);
}
)"));
}

// A-004: bind Viper.Math.Random — standalone functions now importable
TEST(ZiaBindFns, RandomFunctions)
{
    EXPECT_TRUE(compileOk(R"(
module TestRandom;
bind Viper.Math.Random;
func start() {
    var r = Range(1, 100);
}
)"));
}

// A-005: bind Viper.String — Capitalize/Title/Slug etc. now importable
TEST(ZiaBindFns, StringFunctions)
{
    EXPECT_TRUE(compileOk(R"(
module TestString;
bind Viper.String;
func start() {
    var a = Capitalize("hello");
    var b = Title("hello world");
    var c = Slug("Hello World!");
    var d = LastIndexOf("hello world hello", "hello");
    var e = RemovePrefix("hello world", "hello ");
    var f = RemoveSuffix("hello world", " world");
}
)"));
}

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
