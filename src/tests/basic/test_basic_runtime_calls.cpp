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

TEST(BasicRuntimeCalls, Physics3DQuerySurface) {
    ASSERT_TRUE(compileOk(R"(
DIM world AS OBJECT
DIM body AS OBJECT
DIM origin AS OBJECT
DIM dir AS OBJECT
DIM delta AS OBJECT
DIM minv AS OBJECT
DIM maxv AS OBJECT
DIM hit AS OBJECT
DIM hits AS OBJECT
DIM count AS INTEGER
DIM frac AS DOUBLE
world = Viper.Graphics3D.Physics3DWorld.New(0.0, 0.0, 0.0)
body = Viper.Graphics3D.Physics3DBody.NewAABB(1.0, 1.0, 1.0, 0.0)
origin = Viper.Math.Vec3.New(0.0, 0.0, 0.0)
dir = Viper.Math.Vec3.New(1.0, 0.0, 0.0)
delta = Viper.Math.Vec3.New(5.0, 0.0, 0.0)
minv = Viper.Math.Vec3.New(-1.0, -1.0, -1.0)
maxv = Viper.Math.Vec3.New(1.0, 1.0, 1.0)
world.Add(body)
hit = world.Raycast(origin, dir, 10.0, 1)
hit = world.SweepSphere(origin, 0.5, delta, 1)
hit = world.SweepCapsule(origin, delta, 0.5, delta, 1)
hits = world.RaycastAll(origin, dir, 10.0, 1)
hits = world.OverlapSphere(origin, 1.0, 1)
hits = world.OverlapAABB(minv, maxv, 1)
count = Viper.Graphics3D.PhysicsHitList3D.get_Count(hits)
hit = Viper.Graphics3D.PhysicsHitList3D.Get(hits, 0)
frac = Viper.Graphics3D.PhysicsHit3D.get_Fraction(hit)
PRINT count
)"));
}

TEST(BasicRuntimeCalls, CollisionEvent3DSurface) {
    ASSERT_TRUE(compileOk(R"(
DIM world AS OBJECT
DIM evt AS OBJECT
DIM cp AS OBJECT
DIM count AS INTEGER
DIM sep AS DOUBLE
world = Viper.Graphics3D.Physics3DWorld.New(0.0, 0.0, 0.0)
count = world.CollisionEventCount
count = world.EnterEventCount
count = world.StayEventCount
count = world.ExitEventCount
evt = world.GetCollisionEvent(0)
evt = world.GetEnterEvent(0)
evt = world.GetStayEvent(0)
evt = world.GetExitEvent(0)
count = Viper.Graphics3D.CollisionEvent3D.get_ContactCount(evt)
sep = Viper.Graphics3D.CollisionEvent3D.get_NormalImpulse(evt)
sep = Viper.Graphics3D.CollisionEvent3D.get_RelativeSpeed(evt)
cp = Viper.Graphics3D.CollisionEvent3D.GetContact(evt, 0)
cp = Viper.Graphics3D.CollisionEvent3D.GetContactPoint(evt, 0)
cp = Viper.Graphics3D.CollisionEvent3D.GetContactNormal(evt, 0)
sep = Viper.Graphics3D.CollisionEvent3D.GetContactSeparation(evt, 0)
sep = Viper.Graphics3D.ContactPoint3D.get_Separation(cp)
PRINT count
)"));
}

TEST(BasicRuntimeCalls, Model3DSurface) {
    ASSERT_TRUE(compileOk(R"(
DIM scene AS OBJECT
DIM parent AS OBJECT
DIM child AS OBJECT
DIM mesh AS OBJECT
DIM mat AS OBJECT
DIM model AS OBJECT
DIM inst AS OBJECT
DIM instScene AS OBJECT
DIM node AS OBJECT
DIM count AS INTEGER
scene = Viper.Graphics3D.Scene3D.New()
parent = Viper.Graphics3D.SceneNode3D.New()
child = Viper.Graphics3D.SceneNode3D.New()
mesh = Viper.Graphics3D.Mesh3D.NewBox(1.0, 1.0, 1.0)
mat = Viper.Graphics3D.Material3D.NewColor(0.2, 0.4, 0.6)
Viper.Graphics3D.SceneNode3D.set_Name(parent, "parent")
Viper.Graphics3D.SceneNode3D.set_Name(child, "child")
Viper.Graphics3D.SceneNode3D.SetPosition(parent, 1.0, 2.0, 3.0)
parent.Mesh = mesh
parent.Material = mat
child.Mesh = mesh
child.Material = mat
Viper.Graphics3D.SceneNode3D.AddChild(parent, child)
Viper.Graphics3D.Scene3D.Add(scene, parent)
count = Viper.Graphics3D.Scene3D.Save(scene, "tests/runtime/_basic_model3d_surface.vscn")
model = Viper.Graphics3D.Model3D.Load("tests/runtime/_basic_model3d_surface.vscn")
count = model.MeshCount
count = model.MaterialCount
count = model.SkeletonCount
count = model.AnimationCount
count = model.NodeCount
mesh = model.GetMesh(0)
mat = model.GetMaterial(0)
node = model.FindNode("child")
inst = model.Instantiate()
instScene = model.InstantiateScene()
PRINT count
)"));
}

TEST(BasicRuntimeCalls, AnimController3DSurface) {
    ASSERT_TRUE(compileOk(R"(
DIM skel AS OBJECT
DIM controller AS OBJECT
DIM walk AS OBJECT
DIM wave AS OBJECT
DIM pos0 AS OBJECT
DIM pos1 AS OBJECT
DIM rot AS OBJECT
DIM scl AS OBJECT
DIM mat AS OBJECT
DIM delta AS OBJECT
DIM evt AS STRING
DIM state AS STRING
DIM ok AS BOOLEAN
DIM count AS INTEGER
skel = Viper.Graphics3D.Skeleton3D.New()
Viper.Graphics3D.Skeleton3D.AddBone(skel, "root", -1, Viper.Math.Mat4.Identity())
Viper.Graphics3D.Skeleton3D.AddBone(skel, "arm", 0, Viper.Math.Mat4.Identity())
Viper.Graphics3D.Skeleton3D.ComputeInverseBind(skel)
walk = Viper.Graphics3D.Animation3D.New("walk", 1.0)
wave = Viper.Graphics3D.Animation3D.New("wave", 1.0)
pos0 = Viper.Math.Vec3.New(0.0, 0.0, 0.0)
pos1 = Viper.Math.Vec3.New(5.0, 0.0, 0.0)
rot = Viper.Math.Quat.Identity()
scl = Viper.Math.Vec3.One()
Viper.Graphics3D.Animation3D.AddKeyframe(walk, 0, 0.0, pos0, rot, scl)
Viper.Graphics3D.Animation3D.AddKeyframe(walk, 0, 1.0, pos1, rot, scl)
Viper.Graphics3D.Animation3D.AddKeyframe(wave, 1, 0.0, pos0, rot, scl)
Viper.Graphics3D.Animation3D.AddKeyframe(wave, 1, 1.0, pos1, rot, scl)
controller = Viper.Graphics3D.AnimController3D.New(skel)
count = controller.AddState("walk", walk)
count = controller.AddState("wave", wave)
ok = controller.AddTransition("walk", "wave", 0.2)
controller.SetStateSpeed("walk", 1.25)
controller.SetStateLooping("walk", 1)
controller.AddEvent("walk", 0.5, "step")
ok = controller.Play("walk")
ok = controller.Crossfade("wave", 0.2)
controller.SetRootMotionBone(0)
controller.SetLayerMask(1, 1)
controller.SetLayerWeight(1, 0.5)
ok = controller.PlayLayer(1, "wave")
ok = controller.CrossfadeLayer(1, "walk", 0.1)
controller.StopLayer(1)
controller.Update(0.016)
state = controller.CurrentState
state = controller.PreviousState
ok = controller.IsTransitioning
count = controller.StateCount
evt = controller.PollEvent()
delta = controller.RootMotionDelta
delta = controller.ConsumeRootMotion()
mat = controller.GetBoneMatrix(0)
controller.Stop()
PRINT count
)"));
}

TEST(BasicRuntimeCalls, Scene3DBindingSurface) {
    ASSERT_TRUE(compileOk(R"(
DIM scene AS OBJECT
DIM parent AS OBJECT
DIM node AS OBJECT
DIM body AS OBJECT
DIM skel AS OBJECT
DIM controller AS OBJECT
DIM anim AS OBJECT
DIM pos0 AS OBJECT
DIM pos1 AS OBJECT
DIM rot AS OBJECT
DIM scl AS OBJECT
DIM count AS INTEGER
DIM mode AS INTEGER
DIM bound AS OBJECT
scene = Viper.Graphics3D.Scene3D.New()
parent = Viper.Graphics3D.SceneNode3D.New()
node = Viper.Graphics3D.SceneNode3D.New()
body = Viper.Graphics3D.Physics3DBody.NewSphere(0.5, 1.0)
skel = Viper.Graphics3D.Skeleton3D.New()
Viper.Graphics3D.Skeleton3D.AddBone(skel, "root", -1, Viper.Math.Mat4.Identity())
Viper.Graphics3D.Skeleton3D.ComputeInverseBind(skel)
anim = Viper.Graphics3D.Animation3D.New("walk", 1.0)
pos0 = Viper.Math.Vec3.New(0.0, 0.0, 0.0)
pos1 = Viper.Math.Vec3.New(1.0, 0.0, 0.0)
rot = Viper.Math.Quat.Identity()
scl = Viper.Math.Vec3.One()
Viper.Graphics3D.Animation3D.AddKeyframe(anim, 0, 0.0, pos0, rot, scl)
Viper.Graphics3D.Animation3D.AddKeyframe(anim, 0, 1.0, pos1, rot, scl)
controller = Viper.Graphics3D.AnimController3D.New(skel)
controller.AddState("walk", anim)
controller.Play("walk")
Viper.Graphics3D.SceneNode3D.AddChild(parent, node)
Viper.Graphics3D.Scene3D.Add(scene, parent)
node.BindBody(body)
bound = node.Body
node.SyncMode = 1
mode = node.SyncMode
node.BindAnimator(controller)
bound = node.Animator
scene.SyncBindings(0.016)
node.ClearAnimatorBinding()
node.ClearBodyBinding()
PRINT mode
)"));
}

TEST(BasicRuntimeCalls, NavAgent3DSurface) {
    ASSERT_TRUE(compileOk(R"(
DIM mesh AS OBJECT
DIM nav AS OBJECT
DIM world AS OBJECT
DIM agent AS OBJECT
DIM character AS OBJECT
DIM node AS OBJECT
DIM target AS OBJECT
DIM pos AS OBJECT
DIM vel AS OBJECT
DIM hasPath AS BOOLEAN
DIM dist AS DOUBLE
mesh = Viper.Graphics3D.Mesh3D.NewPlane(20.0, 20.0)
nav = Viper.Graphics3D.NavMesh3D.Build(mesh, 0.4, 1.8)
world = Viper.Graphics3D.Physics3DWorld.New(0.0, -9.8, 0.0)
character = Viper.Graphics3D.Character3D.New(0.4, 1.8, 80.0)
character.World = world
node = Viper.Graphics3D.SceneNode3D.New()
agent = Viper.Graphics3D.NavAgent3D.New(nav, 0.4, 1.8)
agent.BindCharacter(character)
agent.BindNode(node)
agent.DesiredSpeed = 3.5
agent.StoppingDistance = 0.25
agent.AutoRepath = 1
target = Viper.Math.Vec3.New(4.0, 0.0, 4.0)
agent.SetTarget(target)
agent.Update(0.1)
pos = agent.Position
vel = agent.Velocity
vel = agent.DesiredVelocity
hasPath = agent.HasPath
dist = agent.RemainingDistance
agent.Warp(Viper.Math.Vec3.New(0.0, 0.0, 0.0))
agent.ClearTarget()
PRINT dist
)"));
}

TEST(BasicRuntimeCalls, Audio3DObjectSurface) {
    ASSERT_TRUE(compileOk(R"(
DIM ok AS BOOLEAN
DIM sound AS OBJECT
DIM cam AS OBJECT
DIM scene AS OBJECT
DIM parent AS OBJECT
DIM node AS OBJECT
DIM listener AS OBJECT
DIM source AS OBJECT
DIM pos AS OBJECT
DIM vel AS OBJECT
DIM voice AS INTEGER
ok = Viper.Sound.Audio.IsAvailable()
cam = Viper.Graphics3D.Camera3D.New(60.0, 1.0, 0.1, 100.0)
Viper.Graphics3D.Camera3D.LookAt(cam, Viper.Math.Vec3.New(0.0, 2.0, 6.0), Viper.Math.Vec3.New(0.0, 1.0, 0.0), Viper.Math.Vec3.New(0.0, 1.0, 0.0))
scene = Viper.Graphics3D.Scene3D.New()
parent = Viper.Graphics3D.SceneNode3D.New()
node = Viper.Graphics3D.SceneNode3D.New()
Viper.Graphics3D.SceneNode3D.SetPosition(parent, 1.0, 0.0, 2.0)
Viper.Graphics3D.SceneNode3D.AddChild(parent, node)
Viper.Graphics3D.Scene3D.Add(scene, parent)
listener = Viper.Graphics3D.AudioListener3D.New()
listener.BindCamera(cam)
listener.IsActive = 1
source = Viper.Graphics3D.AudioSource3D.New(Viper.Sound.Synth.Tone(440, 120, 80))
source.BindNode(node)
source.MaxDistance = 18.0
source.Volume = 70
source.Looping = 0
Viper.Graphics3D.Scene3D.SyncBindings(scene, 0.016)
Viper.Graphics3D.Audio3D.SyncBindings(0.016)
pos = listener.Position
pos = source.Position
vel = listener.Velocity
vel = source.Velocity
voice = source.Play()
source.Stop()
listener.ClearCameraBinding()
listener.BindNode(node)
listener.ClearNodeBinding()
source.ClearNodeBinding()
PRINT voice
)"));
}

TEST(BasicRuntimeCalls, Material3DPBRSurface) {
    ASSERT_TRUE(compileOk(R"(
DIM base AS OBJECT
DIM inst AS OBJECT
DIM tex AS OBJECT
DIM metallic AS DOUBLE
DIM roughness AS DOUBLE
DIM ao AS DOUBLE
DIM emissiveIntensity AS DOUBLE
DIM normalScale AS DOUBLE
DIM alphaMode AS INTEGER
DIM doubleSided AS BOOLEAN
base = Viper.Graphics3D.Material3D.NewPBR(0.8, 0.7, 0.6)
tex = Viper.Graphics.Pixels.New(1, 1)
base.SetAlbedoMap(tex)
base.SetMetallic(0.9)
base.SetRoughness(0.15)
base.SetAO(0.85)
base.SetEmissiveIntensity(2.5)
base.SetNormalMap(tex)
base.SetMetallicRoughnessMap(tex)
base.SetAOMap(tex)
base.SetEmissiveMap(tex)
base.SetNormalScale(0.75)
base.AlphaMode = 2
base.DoubleSided = 1
inst = base.MakeInstance()
inst.Roughness = 0.55
inst = inst.Clone()
metallic = inst.Metallic
roughness = inst.Roughness
ao = inst.AO
emissiveIntensity = inst.EmissiveIntensity
normalScale = inst.NormalScale
alphaMode = inst.AlphaMode
doubleSided = inst.DoubleSided
PRINT metallic
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
