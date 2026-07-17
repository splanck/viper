DIM skel AS OBJECT
DIM rootBone AS INTEGER
DIM armBone AS INTEGER
DIM idle AS OBJECT
DIM walk AS OBJECT
DIM wave AS OBJECT
DIM rot AS OBJECT
DIM scl AS OBJECT
DIM controller AS OBJECT
DIM delta AS OBJECT
DIM rootMat AS OBJECT
DIM armMat AS OBJECT

PRINT "AnimController3D demo"

skel = Zanna.Graphics3D.Skeleton3D.New()
rootBone = Zanna.Graphics3D.Skeleton3D.AddBone(skel, "root", -1, Zanna.Math.Mat4.Identity())
armBone = Zanna.Graphics3D.Skeleton3D.AddBone(skel, "arm", rootBone, Zanna.Math.Mat4.Identity())
Zanna.Graphics3D.Skeleton3D.ComputeInverseBind(skel)

rot = Zanna.Math.Quat.Identity()
scl = Zanna.Math.Vec3.One()

idle = Zanna.Graphics3D.Animation3D.New("idle", 1.0)
Zanna.Graphics3D.Animation3D.set_Looping(idle, 1)
Zanna.Graphics3D.Animation3D.AddKeyframe(idle, rootBone, 0.0, Zanna.Math.Vec3.Zero(), rot, scl)
Zanna.Graphics3D.Animation3D.AddKeyframe(idle, rootBone, 1.0, Zanna.Math.Vec3.Zero(), rot, scl)

walk = Zanna.Graphics3D.Animation3D.New("walk", 1.0)
Zanna.Graphics3D.Animation3D.set_Looping(walk, 1)
Zanna.Graphics3D.Animation3D.AddKeyframe(walk, rootBone, 0.0, Zanna.Math.Vec3.Zero(), rot, scl)
Zanna.Graphics3D.Animation3D.AddKeyframe(walk, rootBone, 1.0, Zanna.Math.Vec3.New(10.0, 0.0, 0.0), rot, scl)

wave = Zanna.Graphics3D.Animation3D.New("wave", 1.0)
Zanna.Graphics3D.Animation3D.set_Looping(wave, 1)
Zanna.Graphics3D.Animation3D.AddKeyframe(wave, armBone, 0.0, Zanna.Math.Vec3.Zero(), rot, scl)
Zanna.Graphics3D.Animation3D.AddKeyframe(wave, armBone, 1.0, Zanna.Math.Vec3.New(0.0, 2.0, 0.0), rot, scl)

controller = Zanna.Graphics3D.AnimController3D.New(skel)
Zanna.Graphics3D.AnimController3D.AddState(controller, "idle", idle)
Zanna.Graphics3D.AnimController3D.AddState(controller, "walk", walk)
Zanna.Graphics3D.AnimController3D.AddState(controller, "wave", wave)
Zanna.Graphics3D.AnimController3D.AddTransition(controller, "idle", "walk", 0.25)
Zanna.Graphics3D.AnimController3D.AddEvent(controller, "walk", 0.5, "step")
Zanna.Graphics3D.AnimController3D.SetRootMotionBone(controller, rootBone)
Zanna.Graphics3D.AnimController3D.SetLayerMask(controller, 1, armBone)
Zanna.Graphics3D.AnimController3D.SetLayerWeight(controller, 1, 1.0)

Zanna.Graphics3D.AnimController3D.Play(controller, "idle")
PRINT "CurrentState = "; Zanna.Graphics3D.AnimController3D.get_CurrentState(controller)

Zanna.Graphics3D.AnimController3D.Play(controller, "walk")
Zanna.Graphics3D.AnimController3D.PlayLayer(controller, 1, "wave")
Zanna.Graphics3D.AnimController3D.Update(controller, 0.5)

delta = Zanna.Graphics3D.AnimController3D.ConsumeRootMotion(controller)
rootMat = Zanna.Graphics3D.AnimController3D.GetBoneMatrix(controller, rootBone)
armMat = Zanna.Graphics3D.AnimController3D.GetBoneMatrix(controller, armBone)

PRINT "StateCount = "; Zanna.Graphics3D.AnimController3D.get_StateCount(controller)
PRINT "PreviousState = "; Zanna.Graphics3D.AnimController3D.get_PreviousState(controller)
PRINT "RootMotion X = "; Zanna.Math.Vec3.get_X(delta)
PRINT "QueuedEvent = "; Zanna.Graphics3D.AnimController3D.PollEvent(controller)
PRINT "Root bone X = "; Zanna.Math.Mat4.Get(rootMat, 0, 3)
PRINT "Arm bone Y = "; Zanna.Math.Mat4.Get(armMat, 1, 3)
