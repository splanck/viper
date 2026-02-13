//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Tests for Zia static calls on runtime classes that previously failed due to
// missing RT_FUNC entries or dotted name resolution issues.
// Fixes bugs A-014, A-019, A-034, A-043, A-052.
//
//===----------------------------------------------------------------------===//

#include "frontends/zia/Compiler.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"

using namespace il::frontends::zia;
using namespace il::support;

namespace
{

/// Helper: compile a Zia source string and return whether it succeeded.
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

} // namespace

// A-019: Result static calls
TEST(ZiaStaticCalls, ResultOkI64)
{
    ASSERT_TRUE(compileOk(R"(
module Test;
func start() {
    var r = Viper.Result.OkI64(42);
    var v = Viper.Result.UnwrapI64(r);
}
)"));
}

// A-019: Result with bind
TEST(ZiaStaticCalls, ResultWithBind)
{
    ASSERT_TRUE(compileOk(R"(
module Test;
bind Viper.Terminal;
func start() {
    var r = Viper.Result.OkStr("hello");
    Say(Viper.Result.UnwrapStr(r));
}
)"));
}

// A-034: Uuid static calls via bind
TEST(ZiaStaticCalls, UuidNew)
{
    ASSERT_TRUE(compileOk(R"(
module Test;
bind Viper.Terminal;
bind Viper.Text;
func start() {
    Say(Uuid.New());
}
)"));
}

// A-043: Password static calls via bind
TEST(ZiaStaticCalls, PasswordHash)
{
    ASSERT_TRUE(compileOk(R"(
module Test;
bind Viper.Terminal;
bind Viper.Crypto;
func start() {
    var hash = Password.Hash("secret");
    Say(hash);
}
)"));
}

// A-043: Option static calls
TEST(ZiaStaticCalls, OptionSomeI64)
{
    ASSERT_TRUE(compileOk(R"(
module Test;
func start() {
    var opt = Viper.Option.SomeI64(99);
    var v = Viper.Option.UnwrapI64(opt);
}
)"));
}

// A-014: Easing static calls via bind
TEST(ZiaStaticCalls, EasingLinear)
{
    ASSERT_TRUE(compileOk(R"(
module Test;
bind Viper.Math;
func start() {
    var v = Easing.Linear(0.5);
}
)"));
}

// A-052: Lazy static calls
TEST(ZiaStaticCalls, LazyOfI64)
{
    ASSERT_TRUE(compileOk(R"(
module Test;
func start() {
    var lazy = Viper.Lazy.OfI64(42);
    var v = Viper.Lazy.GetI64(lazy);
}
)"));
}

int main()
{
    return viper_test::run_all_tests();
}
