//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Regression tests for Zia's safe pointer surface.
//
//===----------------------------------------------------------------------===//

#include "frontends/zia/Compiler.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"
#include <string>

using namespace il::frontends::zia;
using namespace il::support;

namespace {

CompilerResult compileSource(const std::string &source, CompilerOptions opts = {}) {
    SourceManager sm;
    CompilerInput input{.source = source, .path = "pointer_safety.zia"};
    return compile(input, opts, sm);
}

bool hasDiagnostic(const CompilerResult &result, const std::string &needle) {
    for (const auto &diag : result.diagnostics.diagnostics()) {
        if (diag.message.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

TEST(ZiaPointerSafety, RawPtrTypeRejectedByDefault) {
    auto result = compileSource(R"(
module Test;

func start() {
    var p: Ptr = null;
}
)");

    EXPECT_TRUE(!result.succeeded());
    EXPECT_TRUE(hasDiagnostic(result, "Raw Ptr is unsafe"));
}

TEST(ZiaPointerSafety, UnsafePointerModeAllowsExplicitPtr) {
    CompilerOptions opts{};
    opts.allowUnsafePointers = true;

    auto result = compileSource(R"(
module Test;

func start() {
    var p: Ptr = null;
    var q: Viper.Unsafe.Ptr = null;
    if p == null {
        Viper.Terminal.Say("ok");
    }
}
)",
                                opts);

    EXPECT_TRUE(result.succeeded());
}

TEST(ZiaPointerSafety, AnyAcceptsPrimitiveStringAndFunctionReferences) {
    auto result = compileSource(R"(
module Test;

func takeAny(value: Any) {
}

func callback(value: Any) {
}

func start() {
    var i: Any = 42;
    var s: Any = "text";
    var cb = &callback;
    takeAny(i);
    takeAny(s);
    takeAny(cb);
    takeAny(&callback);
}
)");

    EXPECT_TRUE(result.succeeded());
}

TEST(ZiaPointerSafety, RawRuntimeReturnRejectedByDefault) {
    auto result = compileSource(R"(
module Test;

func start() {
    var entries = Viper.IO.Dir.List(".");
}
)");

    EXPECT_TRUE(!result.succeeded());
    EXPECT_TRUE(hasDiagnostic(result, "raw pointer return"));
    EXPECT_TRUE(hasDiagnostic(result, "Viper.IO.Dir.ListSeq"));
}

TEST(ZiaPointerSafety, RawRuntimeOutParameterRejectedByDefault) {
    auto result = compileSource(R"(
module Test;

func start() {
    var ok = Viper.Core.Parse.TryInt("123", 0);
}
)");

    EXPECT_TRUE(!result.succeeded());
    EXPECT_TRUE(hasDiagnostic(result, "raw pointer parameter 'out_value'"));
}

TEST(ZiaPointerSafety, RuntimeCallbackBridgeDoesNotExposePtr) {
    auto result = compileSource(R"(
module Test;

func worker(arg: Any) {
}

func start() {
    var thread = Viper.Threads.Thread.Start(&worker, 0);
}
)");

    EXPECT_TRUE(result.succeeded());
}

TEST(ZiaPointerSafety, NativeOnlyCallbackApisRejectedByDefault) {
    auto poolResult = compileSource(R"(
module Test;

func worker(arg: Any) {
}

func start() {
    var pool = Viper.Threads.Pool.New(1);
    var ok = Viper.Threads.Pool.Submit(pool, &worker, 0);
}
)");

    EXPECT_TRUE(!poolResult.succeeded());
    EXPECT_TRUE(hasDiagnostic(poolResult, "Viper.Threads.Pool.Submit"));
    EXPECT_TRUE(hasDiagnostic(poolResult, "raw pointer parameter 'callback'"));

    auto busResult = compileSource(R"(
module Test;

func onMessage(data: Any) {
}

func start() {
    var handler = Viper.Core.MessageBus.Callback(&onMessage);
}
)");

    EXPECT_TRUE(!busResult.succeeded());
    EXPECT_TRUE(hasDiagnostic(busResult, "Viper.Core.MessageBus.Callback"));
    EXPECT_TRUE(hasDiagnostic(busResult, "raw pointer parameter 'callback'"));
}

} // namespace

int main() {
    return viper_test::run_all_tests();
}
