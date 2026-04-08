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

skel = Viper.Graphics3D.Skeleton3D.New()
rootBone = Viper.Graphics3D.Skeleton3D.AddBone(skel, "root", -1, Viper.Math.Mat4.Identity())
armBone = Viper.Graphics3D.Skeleton3D.AddBone(skel, "arm", rootBone, Viper.Math.Mat4.Identity())
Viper.Graphics3D.Skeleton3D.ComputeInverseBind(skel)

rot = Viper.Math.Quat.Identity()
scl = Viper.Math.Vec3.One()

idle = Viper.Graphics3D.Animation3D.New("idle", 1.0)
Viper.Graphics3D.Animation3D.set_Looping(idle, 1)
Viper.Graphics3D.Animation3D.AddKeyframe(idle, rootBone, 0.0, Viper.Math.Vec3.Zero(), rot, scl)
Viper.Graphics3D.Animation3D.AddKeyframe(idle, rootBone, 1.0, Viper.Math.Vec3.Zero(), rot, scl)

walk = Viper.Graphics3D.Animation3D.New("walk", 1.0)
Viper.Graphics3D.Animation3D.set_Looping(walk, 1)
Viper.Graphics3D.Animation3D.AddKeyframe(walk, rootBone, 0.0, Viper.Math.Vec3.Zero(), rot, scl)
Viper.Graphics3D.Animation3D.AddKeyframe(walk, rootBone, 1.0, Viper.Math.Vec3.New(10.0, 0.0, 0.0), rot, scl)

wave = Viper.Graphics3D.Animation3D.New("wave", 1.0)
Viper.Graphics3D.Animation3D.set_Looping(wave, 1)
Viper.Graphics3D.Animation3D.AddKeyframe(wave, armBone, 0.0, Viper.Math.Vec3.Zero(), rot, scl)
Viper.Graphics3D.Animation3D.AddKeyframe(wave, armBone, 1.0, Viper.Math.Vec3.New(0.0, 2.0, 0.0), rot, scl)

controller = Viper.Graphics3D.AnimController3D.New(skel)
Viper.Graphics3D.AnimController3D.AddState(controller, "idle", idle)
Viper.Graphics3D.AnimController3D.AddState(controller, "walk", walk)
Viper.Graphics3D.AnimController3D.AddState(controller, "wave", wave)
Viper.Graphics3D.AnimController3D.AddTransition(controller, "idle", "walk", 0.25)
Viper.Graphics3D.AnimController3D.AddEvent(controller, "walk", 0.5, "step")
Viper.Graphics3D.AnimController3D.SetRootMotionBone(controller, rootBone)
Viper.Graphics3D.AnimController3D.SetLayerMask(controller, 1, armBone)
Viper.Graphics3D.AnimController3D.SetLayerWeight(controller, 1, 1.0)

Viper.Graphics3D.AnimController3D.Play(controller, "idle")
PRINT "CurrentState = "; Viper.Graphics3D.AnimController3D.get_CurrentState(controller)

Viper.Graphics3D.AnimController3D.Play(controller, "walk")
Viper.Graphics3D.AnimController3D.PlayLayer(controller, 1, "wave")
Viper.Graphics3D.AnimController3D.Update(controller, 0.5)

delta = Viper.Graphics3D.AnimController3D.ConsumeRootMotion(controller)
rootMat = Viper.Graphics3D.AnimController3D.GetBoneMatrix(controller, rootBone)
armMat = Viper.Graphics3D.AnimController3D.GetBoneMatrix(controller, armBone)

PRINT "StateCount = "; Viper.Graphics3D.AnimController3D.get_StateCount(controller)
PRINT "PreviousState = "; Viper.Graphics3D.AnimController3D.get_PreviousState(controller)
PRINT "RootMotion X = "; Viper.Math.Vec3.get_X(delta)
PRINT "QueuedEvent = "; Viper.Graphics3D.AnimController3D.PollEvent(controller)
PRINT "Root bone X = "; Viper.Math.Mat4.Get(rootMat, 0, 3)
PRINT "Arm bone Y = "; Viper.Math.Mat4.Get(armMat, 1, 3)
