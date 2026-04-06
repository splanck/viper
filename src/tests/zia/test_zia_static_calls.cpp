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

namespace {

/// Helper: compile a Zia source string and return whether it succeeded.
bool compileOk(const std::string &source) {
    SourceManager sm;
    CompilerInput input{.source = source, .path = "<test>"};
    CompilerOptions opts{};
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
func start() {    var r = Viper.Result.OkI64(42);
    var v = Viper.Result.UnwrapI64(r);
}
)"));
}

// A-019: Result with bind
TEST(ZiaStaticCalls, ResultWithBind) {
    ASSERT_TRUE(compileOk(R"(
module Test;
bind Viper.Terminal;
/// @brief Start.
func start() {    var r = Viper.Result.OkStr("hello");
    Say(Viper.Result.UnwrapStr(r));
}
)"));
}

// A-034: Uuid static calls via bind
TEST(ZiaStaticCalls, UuidNew) {
    ASSERT_TRUE(compileOk(R"(
module Test;
bind Viper.Terminal;
bind Viper.Text;
/// @brief Start.
func start() {    Say(Uuid.New());
}
)"));
}

// A-043: Password static calls via bind
TEST(ZiaStaticCalls, PasswordHash) {
    ASSERT_TRUE(compileOk(R"(
module Test;
bind Viper.Terminal;
bind Viper.Crypto;
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
func start() {    var opt = Viper.Option.SomeI64(99);
    var v = Viper.Option.UnwrapI64(opt);
}
)"));
}

// A-014: Easing static calls via bind
TEST(ZiaStaticCalls, EasingLinear) {
    ASSERT_TRUE(compileOk(R"(
module Test;
bind Viper.Math;
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
func start() {    var lazy = Viper.Lazy.OfI64(42);
    var v = Viper.Lazy.GetI64(lazy);
}
)"));
}

TEST(ZiaStaticCalls, RuntimeBuilderApis) {
    ASSERT_TRUE(compileOk(R"(
module Test;
/// @brief Start.
func start() {    var box = Viper.GUI.MessageBox.NewInfo("Title", "Body");
    box.AddButton("OK", 1);
    box.SetDefaultButton(1);
    var choice = box.Show();
    box.Destroy();

    var dlg = Viper.GUI.FileDialog.NewOpen();
    dlg.SetTitle("Choose");
    dlg.AddFilter("Images", "*.png");
    dlg.SetMultiple(1);
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
func start() {    var seq = Viper.Collections.Seq.New();
    var list = Viper.Collections.List.FromSeq(seq);
    var set = Viper.Collections.Set.FromSeq(seq);
    var bag = Viper.Collections.Bag.FromSeq(seq);
    var queue = Viper.Collections.Queue.FromSeq(seq);
    var stack = Viper.Collections.Stack.FromSeq(seq);
    var deque = Viper.Collections.Deque.FromSeq(seq);
    var seqFromList = Viper.Collections.Seq.FromList(list);
    var seqFromSet = Viper.Collections.Seq.FromSet(set);
    var seqFromBag = Viper.Collections.Seq.FromBag(bag);
    var listFromSet = Viper.Collections.List.FromSet(set);
    var listFromDeque = Viper.Collections.List.FromDeque(deque);
}
)"));
}

TEST(ZiaStaticCalls, SpriteAnimatorSurface) {
    ASSERT_TRUE(compileOk(R"(
module Test;
/// @brief Start.
func start() {    var anim = Viper.Graphics.SpriteAnimator.New();
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

func keepList(x: Viper.Collections.List) -> Viper.Collections.List {    return x;
}

/// @brief Start.
func start() {    var list = Viper.Collections.List.New();
    var opt = Viper.Option.Some(list);
    var mapped = opt.Map(keepList);
    var out = Viper.Option.Unwrap(mapped);
}
)"));
}

TEST(ZiaStaticCalls, ExplicitReceiverRuntimeMethodsAndProperties) {
    ASSERT_TRUE(compileOk(R"(
module Test;
bind Viper.String as Str;
bind Viper.Collections.Seq as Seq;
bind Viper.Network;

func worker(arg: Ptr) {}

/// @brief Start.
func start() {
    var parts = Str.Split("a,b", ",");
    var n = Seq.get_Length(parts);
    var first = Seq.Get(parts, 0);
    var tcp = Viper.Network.Tcp.Connect("127.0.0.1", 1);
    var host = Viper.Network.Tcp.get_Host(tcp);
    Viper.Network.Tcp.Close(tcp);
    var thread = Viper.Threads.Thread.StartSafe(&worker, 0);
    var pool = Viper.Threads.Pool.New(1);
    var submitted = Viper.Threads.Pool.Submit(pool, &worker, 0);
}
)"));
}

TEST(ZiaStaticCalls, ZeroArgRuntimeStaticMembersAsFields) {
    ASSERT_TRUE(compileOk(R"(
module Test;

/// @brief Start.
func start() {
    var x = Viper.Input.Mouse.X;
    var y = Viper.Input.Mouse.Y;
    var left = Viper.Input.Mouse.Left;
    Viper.Input.Mouse.Hide();
    Viper.Input.Mouse.Show();
}
)"));
}

int main() {
    return viper_test::run_all_tests();
}
