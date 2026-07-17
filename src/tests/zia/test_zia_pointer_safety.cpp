//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
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
    var entries = Zanna.IO.Dir.List(".");
    var files = Zanna.IO.Dir.Files(".");
    var dirs = Zanna.IO.Dir.Dirs(".");
}
)");

    EXPECT_TRUE(result.succeeded());
}

TEST(ZiaPointerSafety, LegacyRuntimeOutParameterApisReturnOptions) {
    auto result = compileSource(R"(
module Test;

func start() {
    var i = Zanna.Core.Parse.TryInt("123");
    var n = Zanna.Core.Parse.TryDouble("12.5");
    var b = Zanna.Core.Parse.TryBool("yes");
    var d = Zanna.Core.Parse.TryDouble("12.5");
    var i64 = Zanna.Core.Parse.TryInt("123");
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
    var thread = Zanna.Threads.Thread.Start(&worker, 0);
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
    var thread = Zanna.Threads.Thread.Start(worker, 0);
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
    var box = Zanna.Core.Box.I64(12);
    var opt = Zanna.Core.Box.ToI64Option(box);
    var fields = Zanna.String.SplitFields("a,\"b,c\"");
    var path = Zanna.Graphics.Path2D.New(4);
    var canvas = Zanna.Graphics.Canvas.New("x", 16, 16);
    Zanna.Graphics.Canvas.PolylinePath(canvas, path, 16777215);
    var emitter = Zanna.Game.ParticleEmitter.New(4);
    var particle = Zanna.Game.ParticleEmitter.ParticleAt(emitter, 0);
}
)");

    EXPECT_TRUE(result.succeeded());
}

TEST(ZiaPointerSafety, RuntimeCallbackBridgeAcceptsTypedRuntimeClassPayload) {
    auto result = compileSource(R"(
module Test;

func worker(client: Zanna.Network.Tcp) {
}

func start() {
    var server = Zanna.Network.TcpServer.Listen(0);
    var client = Zanna.Network.TcpServer.Accept(server);
    var thread = Zanna.Threads.Thread.StartSafe(&worker, client);
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
    var pool = Zanna.Threads.Pool.New(1);
    var ok = Zanna.Threads.Pool.Submit(pool, &worker, 0);
}
)");

    EXPECT_TRUE(poolResult.succeeded());

    auto busResult = compileSource(R"(
module Test;

func onMessage(data: Any) {
}

func start() {
    var handler = Zanna.Core.MessageBus.Callback(&onMessage);
}
)");

    EXPECT_TRUE(busResult.succeeded());

    auto functionalResult = compileSource(R"(
module Test;

func keep(item: Any) -> Boolean {
    return true;
}

func start() {
    var seq = Zanna.Collections.Seq.New();
    var kept = Zanna.Collections.Seq.Filter(seq, &keep);
}
)");

    EXPECT_TRUE(functionalResult.succeeded());
}

} // namespace

int main() {
    return zanna_test::run_all_tests();
}
