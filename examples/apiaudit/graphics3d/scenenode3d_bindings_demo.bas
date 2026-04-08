DIM scene AS OBJECT
DIM parent AS OBJECT
DIM child AS OBJECT
DIM body AS OBJECT
DIM childPos AS OBJECT
DIM skel AS OBJECT
DIM walk AS OBJECT
DIM rot AS OBJECT
DIM scl AS OBJECT
DIM controller AS OBJECT
DIM animNode AS OBJECT
DIM animPos AS OBJECT
DIM rootBone AS INTEGER

PRINT "SceneNode3D bindings demo"

scene = Viper.Graphics3D.Scene3D.New()
parent = Viper.Graphics3D.SceneNode3D.New()
child = Viper.Graphics3D.SceneNode3D.New()
body = Viper.Graphics3D.Physics3DBody.NewSphere(0.5, 1.0)

Viper.Graphics3D.SceneNode3D.SetPosition(parent, 5.0, 0.0, 0.0)
Viper.Graphics3D.SceneNode3D.AddChild(parent, child)
Viper.Graphics3D.Scene3D.Add(scene, parent)

child.BindBody(body)
child.SyncMode = 0
Viper.Graphics3D.Physics3DBody.SetPosition(body, 6.0, 1.5, -2.0)
scene.SyncBindings(0.016)

childPos = child.Position
PRINT "Child local X from body = "; Viper.Math.Vec3.get_X(childPos)

skel = Viper.Graphics3D.Skeleton3D.New()
rootBone = Viper.Graphics3D.Skeleton3D.AddBone(skel, "root", -1, Viper.Math.Mat4.Identity())
Viper.Graphics3D.Skeleton3D.ComputeInverseBind(skel)

walk = Viper.Graphics3D.Animation3D.New("walk", 1.0)
Viper.Graphics3D.Animation3D.set_Looping(walk, 1)
rot = Viper.Math.Quat.Identity()
scl = Viper.Math.Vec3.One()
Viper.Graphics3D.Animation3D.AddKeyframe(walk, rootBone, 0.0, Viper.Math.Vec3.Zero(), rot, scl)
Viper.Graphics3D.Animation3D.AddKeyframe(walk, rootBone, 1.0, Viper.Math.Vec3.New(4.0, 0.0, 0.0), rot, scl)

controller = Viper.Graphics3D.AnimController3D.New(skel)
Viper.Graphics3D.AnimController3D.AddState(controller, "walk", walk)
Viper.Graphics3D.AnimController3D.SetRootMotionBone(controller, rootBone)
Viper.Graphics3D.AnimController3D.Play(controller, "walk")
Viper.Graphics3D.AnimController3D.Update(controller, 0.5)

animNode = Viper.Graphics3D.SceneNode3D.New()
animNode.BindAnimator(controller)
animNode.SyncMode = 2
Viper.Graphics3D.Scene3D.Add(scene, animNode)
scene.SyncBindings(0.016)

animPos = animNode.Position
PRINT "Animator root motion X = "; Viper.Math.Vec3.get_X(animPos)
PRINT "Sync mode = "; animNode.SyncMode
