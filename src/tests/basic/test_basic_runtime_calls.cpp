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

std::optional<il::core::Module> compileModule(const std::string &source) {
    SourceManager sm;
    BasicCompilerOptions opts{};
    BasicCompilerInput input{source, "<test>"};
    auto result = compileBasic(input, opts, sm);
    if (!result.succeeded()) {
        std::cerr << "Compilation failed:\n";
        for (const auto &d : result.diagnostics.diagnostics()) {
            std::cerr << "  " << d.message << "\n";
        }
        return std::nullopt;
    }
    return result.module;
}

/// Helper: compile a BASIC source string and return whether it succeeded.
bool compileOk(const std::string &source) {
    return compileModule(source).has_value();
}

const il::core::Function *findFunction(const il::core::Module &module, const std::string &name) {
    for (const auto &fn : module.functions) {
        if (fn.name == name)
            return &fn;
    }
    return nullptr;
}

size_t countCallsTo(const il::core::Function &fn, const std::string &callee) {
    size_t count = 0;
    for (const auto &block : fn.blocks) {
        for (const auto &instr : block.instructions) {
            if (instr.op == il::core::Opcode::Call && instr.callee == callee)
                ++count;
        }
    }
    return count;
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

TEST(BasicRuntimeCalls, Physics3DBodyRotationalSurface) {
    ASSERT_TRUE(compileOk(R"(
DIM body AS OBJECT
DIM q AS OBJECT
DIM ang AS OBJECT
DIM sleeping AS BOOLEAN
body = Viper.Graphics3D.Physics3DBody.NewSphere(1.0, 1.0)
q = Viper.Math.Quat.Identity()
Viper.Graphics3D.Physics3DBody.SetOrientation(body, q)
Viper.Graphics3D.Physics3DBody.SetAngularVelocity(body, 0.0, 1.0, 0.0)
Viper.Graphics3D.Physics3DBody.ApplyTorque(body, 0.0, 2.0, 0.0)
Viper.Graphics3D.Physics3DBody.ApplyAngularImpulse(body, 0.0, 1.0, 0.0)
body.LinearDamping = 0.2
body.AngularDamping = 0.3
body.Kinematic = 1
body.CanSleep = 1
body.UseCCD = 1
ang = body.AngularVelocity
q = body.Orientation
sleeping = body.Sleeping
body.Sleep()
body.Wake()
PRINT sleeping
)"));
}

TEST(BasicRuntimeCalls, Collider3DSurface) {
    ASSERT_TRUE(compileOk(R"(
DIM body AS OBJECT
DIM boxCol AS OBJECT
DIM hullCol AS OBJECT
DIM compound AS OBJECT
DIM mesh AS OBJECT
DIM xf AS OBJECT
DIM minv AS OBJECT
DIM maxv AS OBJECT
DIM ty AS INTEGER
mesh = Viper.Graphics3D.Mesh3D.NewBox(2.0, 1.0, 2.0)
boxCol = Viper.Graphics3D.Collider3D.NewBox(1.0, 0.5, 1.0)
hullCol = Viper.Graphics3D.Collider3D.NewConvexHull(mesh)
compound = Viper.Graphics3D.Collider3D.NewCompound()
xf = Viper.Graphics3D.Transform3D.New()
Viper.Graphics3D.Transform3D.SetPosition(xf, 1.5, 0.0, 0.0)
Viper.Graphics3D.Collider3D.AddChild(compound, boxCol, xf)
Viper.Graphics3D.Collider3D.AddChild(compound, hullCol, xf)
minv = Viper.Graphics3D.Collider3D.GetLocalBoundsMin(compound)
maxv = Viper.Graphics3D.Collider3D.GetLocalBoundsMax(compound)
ty = compound.Type
body = Viper.Graphics3D.Physics3DBody.New(1.0)
body.Collider = compound
body.SetCollider(boxCol)
boxCol = body.Collider
PRINT ty
)"));
}

TEST(BasicRuntimeCalls, CompiledPatternObjectResultKeepsSeqSurface) {
    auto module = compileModule(R"(
DIM pat AS OBJECT
DIM matches AS OBJECT
DIM count AS INTEGER
pat = Viper.Text.CompiledPattern.New("[0-9]+")
matches = pat.FindAll("a1b22c333")
count = matches.Length
PRINT count
)");
    ASSERT_TRUE(module.has_value());
    const auto *mainFn = findFunction(*module, "main");
    ASSERT_TRUE(mainFn != nullptr);
    EXPECT_GE(countCallsTo(*mainFn, "Viper.Collections.Seq.get_Length"), static_cast<size_t>(1));
}

TEST(BasicRuntimeCalls, DefaultMapKeysObjectResultKeepsSeqSurface) {
    auto module = compileModule(R"(
DIM dm AS OBJECT
DIM keys AS OBJECT
DIM count AS INTEGER
dm = Viper.Collections.DefaultMap.New(Viper.Core.Box.Str("N/A"))
dm.Set("name", "Alice")
dm.Set("city", "Boston")
keys = dm.Keys()
count = keys.Length
PRINT count
)");
    ASSERT_TRUE(module.has_value());
    const auto *mainFn = findFunction(*module, "main");
    ASSERT_TRUE(mainFn != nullptr);
    EXPECT_GE(countCallsTo(*mainFn, "Viper.Collections.Seq.get_Length"), static_cast<size_t>(1));
}

TEST(BasicRuntimeCalls, PatternFindAllObjectResultKeepsSeqSurface) {
    auto module = compileModule(R"(
DIM matches AS OBJECT
DIM count AS INTEGER
matches = Viper.Text.Pattern.FindAll("a1b22c333", "[0-9]+")
count = matches.Length
PRINT count
)");
    ASSERT_TRUE(module.has_value());
    const auto *mainFn = findFunction(*module, "main");
    ASSERT_TRUE(mainFn != nullptr);
    EXPECT_GE(countCallsTo(*mainFn, "Viper.Collections.Seq.get_Length"), static_cast<size_t>(1));
}

TEST(BasicRuntimeCalls, LazySeqToSeqNObjectResultKeepsSeqSurface) {
    auto module = compileModule(R"(
DIM seq AS OBJECT
DIM out AS OBJECT
DIM count AS INTEGER
seq = Viper.LazySeq.Range(1, 5, 1)
out = Viper.LazySeq.ToSeqN(seq, 3)
count = out.Length
PRINT count
)");
    ASSERT_TRUE(module.has_value());
    const auto *mainFn = findFunction(*module, "main");
    ASSERT_TRUE(mainFn != nullptr);
    EXPECT_GE(countCallsTo(*mainFn, "Viper.Collections.Seq.get_Length"), static_cast<size_t>(1));
}

TEST(BasicRuntimeCalls, ResultAndOptionFactoriesKeepConcreteObjectSurface) {
    auto module = compileModule(R"(
DIM ok AS OBJECT
DIM none AS OBJECT
DIM okFlag AS BOOLEAN
DIM noneFlag AS BOOLEAN
ok = Viper.Result.OkI64(42)
none = Viper.Option.None()
okFlag = ok.IsOk
noneFlag = none.IsNone
PRINT okFlag
PRINT noneFlag
)");
    ASSERT_TRUE(module.has_value());
    const auto *mainFn = findFunction(*module, "main");
    ASSERT_TRUE(mainFn != nullptr);
    EXPECT_GE(countCallsTo(*mainFn, "Viper.Result.get_IsOk"), static_cast<size_t>(1));
    EXPECT_GE(countCallsTo(*mainFn, "Viper.Option.get_IsNone"), static_cast<size_t>(1));
}

TEST(BasicRuntimeCalls, IoConstructorAliasesLowerToOpenTargets) {
    auto module = compileModule(R"(
DIM writer AS OBJECT
DIM reader AS OBJECT
DIM file AS OBJECT
writer = Viper.IO.LineWriter.New("out.txt")
reader = Viper.IO.LineReader.New("out.txt")
file = Viper.IO.BinFile.New("out.bin", "rw")
PRINT "ok"
)");
    ASSERT_TRUE(module.has_value());
    const auto *mainFn = findFunction(*module, "main");
    ASSERT_TRUE(mainFn != nullptr);
    EXPECT_GE(countCallsTo(*mainFn, "Viper.IO.LineWriter.New"), static_cast<size_t>(1));
    EXPECT_GE(countCallsTo(*mainFn, "Viper.IO.LineReader.New"), static_cast<size_t>(1));
    EXPECT_GE(countCallsTo(*mainFn, "Viper.IO.BinFile.New"), static_cast<size_t>(1));
}

/// @brief Main.
int main() {
    return viper_test::run_all_tests();
}
