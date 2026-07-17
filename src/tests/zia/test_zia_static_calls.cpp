//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
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

namespace {

/// Helper: compile a Zia source string and return whether it succeeded.
bool compileOk(const std::string &source, CompilerOptions opts = {}) {
    SourceManager sm;
    CompilerInput input{.source = source, .path = "<test>"};
    auto result = compile(input, opts, sm);
    if (!result.succeeded()) {
        std::cerr << "Compilation failed:\n";
        for (const auto &d : result.diagnostics.diagnostics()) {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }
    return result.succeeded();
}

} // namespace

// A-019: Result static calls
TEST(ZiaStaticCalls, ResultOkI64) {
    ASSERT_TRUE(compileOk(R"(
module Test;
/// @brief Start.
func start() {    var r = Zanna.Result.OkI64(42);
    var v = Zanna.Result.UnwrapI64(r);
}
)"));
}

// A-019: Result with bind
TEST(ZiaStaticCalls, ResultWithBind) {
    ASSERT_TRUE(compileOk(R"(
module Test;
bind Zanna.Terminal;
/// @brief Start.
func start() {    var r = Zanna.Result.OkStr("hello");
    Say(Zanna.Result.UnwrapStr(r));
}
)"));
}

// A-034: Uuid static calls via bind
TEST(ZiaStaticCalls, UuidNew) {
    ASSERT_TRUE(compileOk(R"(
module Test;
bind Zanna.Terminal;
bind Zanna.Text;
/// @brief Start.
func start() {    Say(Uuid.Generate());
}
)"));
}

// A-043: Password static calls via bind
TEST(ZiaStaticCalls, PasswordHash) {
    ASSERT_TRUE(compileOk(R"(
module Test;
bind Zanna.Terminal;
bind Zanna.Crypto;
/// @brief Start.
func start() {    var hash = Password.Hash("secret");
    Say(hash);
}
)"));
}

// A-043: Option static calls
TEST(ZiaStaticCalls, OptionSomeI64) {
    ASSERT_TRUE(compileOk(R"(
module Test;
/// @brief Start.
func start() {    var opt = Zanna.Option.SomeI64(99);
    var v = Zanna.Option.UnwrapI64(opt);
}
)"));
}

// A-014: Easing static calls via bind
TEST(ZiaStaticCalls, EasingLinear) {
    ASSERT_TRUE(compileOk(R"(
module Test;
bind Zanna.Math;
/// @brief Start.
func start() {    var v = Easing.Linear(0.5);
}
)"));
}

// A-052: Lazy static calls
TEST(ZiaStaticCalls, LazyOfI64) {
    ASSERT_TRUE(compileOk(R"(
module Test;
/// @brief Start.
func start() {    var lazy = Zanna.Functional.Lazy.OfI64(42);
    var v = Zanna.Functional.Lazy.GetI64(lazy);
}
)"));
}

TEST(ZiaStaticCalls, RuntimeBuilderApis) {
    ASSERT_TRUE(compileOk(R"(
module Test;
/// @brief Start.
func start() {    var box = Zanna.GUI.MessageBox.NewInfo("Title", "Body");
    box.AddButton("OK", 1);
    box.SetDefaultButton(1);
    var choice = box.Show();
    box.Destroy();

    var dlg = Zanna.GUI.FileDialog.NewOpen();
    dlg.SetTitle("Choose");
    dlg.AddFilter("Images", "*.png");
    dlg.SetMultiple(true);
    var shown = dlg.Show();
    var path = dlg.GetPath();
    var count = dlg.PathCount;
    var path0 = dlg.GetPathAt(0);
    dlg.Destroy();
}
)"));
}

TEST(ZiaStaticCalls, RuntimeCollectionConverters) {
    ASSERT_TRUE(compileOk(R"(
module Test;
/// @brief Start.
func start() {    var seq = Zanna.Collections.Seq.New();
    var list = Zanna.Collections.Seq.ToList(seq);
    var set = Zanna.Collections.Seq.ToSet(seq);
    var bag = Zanna.Collections.Seq.ToStringSet(seq);
    var queue = Zanna.Collections.Seq.ToQueue(seq);
    var stack = Zanna.Collections.Seq.ToStack(seq);
    var deque = Zanna.Collections.Seq.ToDeque(seq);
    var seqFromList = Zanna.Collections.List.ToSeq(list);
    var seqFromSet = Zanna.Collections.Set.ToSeq(set);
    var seqFromBag = Zanna.Collections.StringSet.ToSeq(bag);
    var listFromSet = Zanna.Collections.Set.ToList(set);
    var listFromDeque = Zanna.Collections.Deque.ToList(deque);
}
)"));
}

TEST(ZiaStaticCalls, SpriteAnimatorSurface) {
    ASSERT_TRUE(compileOk(R"(
module Test;
/// @brief Start.
func start() {    var anim = Zanna.Graphics.SpriteAnimator.New();
    var added = anim.AddClip("idle", 0, 0, 16, 16);
    var playing = anim.Play("idle");
    var current = anim.Current;
    var isPlaying = anim.IsPlaying;
    anim.Stop();
    anim.Destroy();
}
)"));
}

TEST(ZiaStaticCalls, RuntimeObjectCallbackMethod) {
    ASSERT_TRUE(compileOk(R"(
module Test;

func keepList(x: Zanna.Collections.List) -> Zanna.Collections.List {    return x;
}

/// @brief Start.
func start() {    var list = Zanna.Collections.List.New();
    var opt = Zanna.Option.Some(list);
    var mapped = opt.Map(&keepList);
    var out = Zanna.Option.Unwrap(mapped);
}
)"));
}

TEST(ZiaStaticCalls, ExplicitReceiverRuntimeMethodsAndProperties) {
    ASSERT_TRUE(compileOk(R"(
module Test;
bind Zanna.String as Str;
bind Zanna.Collections.Seq as Seq;
bind Zanna.Network;

func worker(arg: Any) {}

/// @brief Start.
func start() {
    var parts = Str.Split("a,b", ",");
    var n = Seq.get_Count(parts);
    var first = Seq.Get(parts, 0);
    var tcp = Zanna.Network.Tcp.Connect("127.0.0.1", 1);
    var host = Zanna.Network.Tcp.get_Host(tcp);
    Zanna.Network.Tcp.Close(tcp);
    var thread = Zanna.Threads.Thread.StartSafe(&worker, 0);
    var pool = Zanna.Threads.Pool.New(1);
    var pending = Zanna.Threads.Pool.get_Pending(pool);
}
)"));
}

TEST(ZiaStaticCalls, AddressOfAllowsForwardDeclaredCallbacks) {
    ASSERT_TRUE(compileOk(R"(
module Test;
bind Zanna.Threads;

/// @brief Start.
func start() {
    var thread = Zanna.Threads.Thread.StartSafe(&worker, 0);
}

func worker(arg: Any) {}
)"));
}

TEST(ZiaStaticCalls, ZeroArgRuntimeStaticMembersAsFields) {
    ASSERT_TRUE(compileOk(R"(
module Test;

/// @brief Start.
func start() {
    var x = Zanna.Input.Mouse.X;
    var y = Zanna.Input.Mouse.Y;
    var dx = Zanna.Input.Mouse.DeltaX;
    var left = Zanna.Input.Mouse.IsDown(Zanna.Input.Mouse.ButtonLeft);
    Zanna.Input.Mouse.Hide();
    Zanna.Input.Mouse.Show();
}
)"));
}

int main() {
    return zanna_test::run_all_tests();
}
