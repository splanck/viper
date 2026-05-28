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
    EXPECT_TRUE(hasDiagnostic(result, "Ptr is not part of the Zia source surface"));
}

TEST(ZiaPointerSafety, UnsafePointerModeDoesNotExposeExplicitPtr) {
    CompilerOptions opts{};
    opts.allowUnsafePointers = true;

    auto result = compileSource(R"(
module Test;

func start() {
    var p: Ptr = null;
}
)",
                                opts);

    EXPECT_TRUE(!result.succeeded());
    EXPECT_TRUE(hasDiagnostic(result, "Ptr is not part of the Zia source surface"));
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

TEST(ZiaPointerSafety, LegacyDirectoryApisExposeSafeSeqs) {
    auto result = compileSource(R"(
module Test;

func start() {
    var entries = Viper.IO.Dir.List(".");
    var files = Viper.IO.Dir.Files(".");
    var dirs = Viper.IO.Dir.Dirs(".");
}
)");

    EXPECT_TRUE(result.succeeded());
}

TEST(ZiaPointerSafety, LegacyRuntimeOutParameterApisReturnOptions) {
    auto result = compileSource(R"(
module Test;

func start() {
    var i = Viper.Core.Parse.TryInt("123");
    var n = Viper.Core.Parse.TryNum("12.5");
    var b = Viper.Core.Parse.TryBool("yes");
    var d = Viper.Core.Parse.Double("12.5");
    var i64 = Viper.Core.Parse.Int64("123");
}
)");

    EXPECT_TRUE(result.succeeded());
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

TEST(ZiaPointerSafety, RuntimeCallbackBridgeAcceptsBareFunctionReference) {
    auto result = compileSource(R"(
module Test;

func worker(arg: Any) {
}

func start() {
    var thread = Viper.Threads.Thread.Start(worker, 0);
}
)");

    EXPECT_TRUE(result.succeeded());
}

TEST(ZiaPointerSafety, FunctionReferenceRejectsNonFunctions) {
    auto variableResult = compileSource(R"(
module Test;

func takeAny(value: Any) {
}

func start() {
    var x = 1;
    takeAny(&x);
}
)");

    EXPECT_TRUE(!variableResult.succeeded());
    EXPECT_TRUE(
        hasDiagnostic(variableResult, "Function reference operator '&' requires a function name"));

    auto expressionResult = compileSource(R"(
module Test;

func takeAny(value: Any) {
}

func start() {
    takeAny(&(1 + 2));
}
)");

    EXPECT_TRUE(!expressionResult.succeeded());
    EXPECT_TRUE(hasDiagnostic(expressionResult,
                              "Function reference operator '&' requires a function name"));
}

TEST(ZiaPointerSafety, SafePointerAlternativesCompile) {
    auto result = compileSource(R"(
module Test;

func start() {
    var box = Viper.Core.Box.I64(12);
    var opt = Viper.Core.Box.TryToI64(box);
    var fields = Viper.String.SplitFields("a,\"b,c\"");
    var path = Viper.Graphics.Path2D.New(4);
    var canvas = Viper.Graphics.Canvas.New("x", 16, 16);
    Viper.Graphics.Canvas.Polyline(canvas, path, 16777215);
    var emitter = Viper.Game.ParticleEmitter.New(4);
    var particle = Viper.Game.ParticleEmitter.Get(emitter, 0);
}
)");

    EXPECT_TRUE(result.succeeded());
}

TEST(ZiaPointerSafety, RuntimeCallbackBridgeAcceptsTypedRuntimeClassPayload) {
    auto result = compileSource(R"(
module Test;

func worker(client: Viper.Network.Tcp) {
}

func start() {
    var server = Viper.Network.TcpServer.Listen(0);
    var client = Viper.Network.TcpServer.Accept(server);
    var thread = Viper.Threads.Thread.StartSafe(&worker, client);
}
)");

    EXPECT_TRUE(result.succeeded());
}

TEST(ZiaPointerSafety, RuntimeCallbackApisCompileThroughManagedReferences) {
    auto poolResult = compileSource(R"(
module Test;

func worker(arg: Any) {
}

func start() {
    var pool = Viper.Threads.Pool.New(1);
    var ok = Viper.Threads.Pool.Submit(pool, &worker, 0);
}
)");

    EXPECT_TRUE(poolResult.succeeded());

    auto busResult = compileSource(R"(
module Test;

func onMessage(data: Any) {
}

func start() {
    var handler = Viper.Core.MessageBus.Callback(&onMessage);
}
)");

    EXPECT_TRUE(busResult.succeeded());

    auto functionalResult = compileSource(R"(
module Test;

func keep(item: Any) -> Boolean {
    return true;
}

func start() {
    var seq = Viper.Collections.Seq.New();
    var kept = Viper.Collections.Seq.Keep(seq, &keep);
}
)");

    EXPECT_TRUE(functionalResult.succeeded());
}

} // namespace

int main() {
    return viper_test::run_all_tests();
}
