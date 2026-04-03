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

namespace {

/// Helper: compile a BASIC source string and return whether it succeeded.
bool compileOk(const std::string &source) {
    SourceManager sm;
    BasicCompilerOptions opts{};
    BasicCompilerInput input{source, "<test>"};
    auto result = compileBasic(input, opts, sm);
    if (!result.succeeded()) {
        std::cerr << "Compilation failed:\n";
        for (const auto &d : result.diagnostics.diagnostics()) {
            std::cerr << "  " << d.message << "\n";
        }
    }
    return result.succeeded();
}

} // namespace

// A-044: Result static calls in BASIC
TEST(BasicRuntimeCalls, ResultOkI64) {
    ASSERT_TRUE(compileOk(R"(
DIM r AS OBJECT
DIM v AS INTEGER
r = Viper.Result.OkI64(42)
v = Viper.Result.UnwrapI64(r)
PRINT v
)"));
}

// A-044: Option static calls in BASIC
TEST(BasicRuntimeCalls, OptionSomeI64) {
    ASSERT_TRUE(compileOk(R"(
DIM opt AS OBJECT
DIM v AS INTEGER
opt = Viper.Option.SomeI64(99)
v = Viper.Option.UnwrapI64(opt)
PRINT v
)"));
}

// A-052: Lazy static calls in BASIC
TEST(BasicRuntimeCalls, LazyOfI64) {
    ASSERT_TRUE(compileOk(R"(
DIM lazy AS OBJECT
DIM v AS INTEGER
lazy = Viper.Lazy.OfI64(42)
v = Viper.Lazy.GetI64(lazy)
PRINT v
)"));
}

// A-014: Easing static calls in BASIC
TEST(BasicRuntimeCalls, EasingLinear) {
    ASSERT_TRUE(compileOk(R"(
DIM v AS DOUBLE
v = Viper.Math.Easing.Linear(0.5)
PRINT v
)"));
}

// A-037: StringBuilder in BASIC
TEST(BasicRuntimeCalls, StringBuilderAppend) {
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
TEST(BasicRuntimeCalls, ScannerNew) {
    ASSERT_TRUE(compileOk(R"(
DIM sc AS OBJECT
sc = Viper.Text.Scanner.New("hello world")
PRINT "created"
)"));
}

TEST(BasicRuntimeCalls, RuntimeBuilderApis) {
    ASSERT_TRUE(compileOk(R"(
DIM box AS OBJECT
DIM choice AS INTEGER
DIM dlg AS OBJECT
DIM shown AS INTEGER
DIM path AS STRING
DIM count AS INTEGER
box = Viper.GUI.MessageBox.NewInfo("Title", "Body")
Viper.GUI.MessageBox.AddButton(box, "OK", 1)
Viper.GUI.MessageBox.SetDefaultButton(box, 1)
choice = Viper.GUI.MessageBox.Show(box)
Viper.GUI.MessageBox.Destroy(box)
dlg = Viper.GUI.FileDialog.NewOpen()
Viper.GUI.FileDialog.SetTitle(dlg, "Choose")
Viper.GUI.FileDialog.AddFilter(dlg, "Images", "*.png")
Viper.GUI.FileDialog.SetMultiple(dlg, 1)
shown = Viper.GUI.FileDialog.Show(dlg)
path = Viper.GUI.FileDialog.GetPath(dlg)
count = Viper.GUI.FileDialog.get_PathCount(dlg)
path = Viper.GUI.FileDialog.GetPathAt(dlg, 0)
Viper.GUI.FileDialog.Destroy(dlg)
PRINT choice
)"));
}

TEST(BasicRuntimeCalls, RuntimeCollectionConverters) {
    ASSERT_TRUE(compileOk(R"(
DIM seq AS OBJECT
DIM list AS OBJECT
DIM st AS OBJECT
DIM bag AS OBJECT
DIM queue AS OBJECT
DIM stack AS OBJECT
DIM deque AS OBJECT
DIM seq2 AS OBJECT
seq = Viper.Collections.Seq.New()
list = Viper.Collections.List.FromSeq(seq)
st = Viper.Collections.Set.FromSeq(seq)
bag = Viper.Collections.Bag.FromSeq(seq)
queue = Viper.Collections.Queue.FromSeq(seq)
stack = Viper.Collections.Stack.FromSeq(seq)
deque = Viper.Collections.Deque.FromSeq(seq)
seq2 = Viper.Collections.Seq.FromList(list)
seq2 = Viper.Collections.Seq.FromSet(st)
seq2 = Viper.Collections.Seq.FromBag(bag)
list = Viper.Collections.List.FromSet(st)
list = Viper.Collections.List.FromDeque(deque)
PRINT "ok"
)"));
}

TEST(BasicRuntimeCalls, SpriteAnimatorSurface) {
    ASSERT_TRUE(compileOk(R"(
DIM anim AS OBJECT
DIM ok AS BOOLEAN
DIM current AS STRING
DIM playing AS BOOLEAN
anim = Viper.Graphics.SpriteAnimator.New()
ok = Viper.Graphics.SpriteAnimator.AddClip(anim, "idle", 0, 0, 16, 16)
playing = Viper.Graphics.SpriteAnimator.Play(anim, "idle")
current = Viper.Graphics.SpriteAnimator.get_Current(anim)
playing = Viper.Graphics.SpriteAnimator.get_IsPlaying(anim)
Viper.Graphics.SpriteAnimator.Stop(anim)
Viper.Graphics.SpriteAnimator.Destroy(anim)
PRINT current
)"));
}

/// @brief Main.
int main() {
    return viper_test::run_all_tests();
}
