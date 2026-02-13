//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Tests for BASIC runtime class calls that previously failed due to
// missing RT_FUNC entries, RuntimeMethodIndex name resolution, or
// RT_MAGIC heap crashes.
// Fixes bugs A-014, A-036, A-037, A-038, A-044, A-052.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/BasicCompiler.hpp"
#include "tests/TestHarness.hpp"

using namespace il::frontends::basic;
using namespace il::support;

namespace
{

/// Helper: compile a BASIC source string and return whether it succeeded.
bool compileOk(const std::string &source)
{
    SourceManager sm;
    BasicCompilerOptions opts{};
    BasicCompilerInput input{source, "<test>"};
    auto result = compileBasic(input, opts, sm);
    if (!result.succeeded())
    {
        std::cerr << "Compilation failed:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  " << d.message << "\n";
        }
    }
    return result.succeeded();
}

} // namespace

// A-044: Result static calls in BASIC
TEST(BasicRuntimeCalls, ResultOkI64)
{
    ASSERT_TRUE(compileOk(R"(
DIM r AS OBJECT
DIM v AS INTEGER
r = Viper.Result.OkI64(42)
v = Viper.Result.UnwrapI64(r)
PRINT v
)"));
}

// A-044: Option static calls in BASIC
TEST(BasicRuntimeCalls, OptionSomeI64)
{
    ASSERT_TRUE(compileOk(R"(
DIM opt AS OBJECT
DIM v AS INTEGER
opt = Viper.Option.SomeI64(99)
v = Viper.Option.UnwrapI64(opt)
PRINT v
)"));
}

// A-052: Lazy static calls in BASIC
TEST(BasicRuntimeCalls, LazyOfI64)
{
    ASSERT_TRUE(compileOk(R"(
DIM lazy AS OBJECT
DIM v AS INTEGER
lazy = Viper.Lazy.OfI64(42)
v = Viper.Lazy.GetI64(lazy)
PRINT v
)"));
}

// A-014: Easing static calls in BASIC
TEST(BasicRuntimeCalls, EasingLinear)
{
    ASSERT_TRUE(compileOk(R"(
DIM v AS DOUBLE
v = Viper.Math.Easing.Linear(0.5)
PRINT v
)"));
}

// A-037: StringBuilder in BASIC
TEST(BasicRuntimeCalls, StringBuilderAppend)
{
    ASSERT_TRUE(compileOk(R"(
DIM sb AS OBJECT
DIM s AS STRING
sb = Viper.Text.StringBuilder.New()
sb = Viper.Text.StringBuilder.Append(sb, "hello")
s = Viper.Text.StringBuilder.ToString(sb)
PRINT s
)"));
}

// A-038: Scanner in BASIC
TEST(BasicRuntimeCalls, ScannerNew)
{
    ASSERT_TRUE(compileOk(R"(
DIM sc AS OBJECT
sc = Viper.Text.Scanner.New("hello world")
PRINT "created"
)"));
}

int main()
{
    return viper_test::run_all_tests();
}
