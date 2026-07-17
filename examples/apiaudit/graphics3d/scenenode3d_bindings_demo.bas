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

PRINT "SceneNode bindings demo"

scene = Zanna.Graphics3D.SceneGraph.New()
parent = Zanna.Graphics3D.SceneNode.New()
child = Zanna.Graphics3D.SceneNode.New()
body = Zanna.Graphics3D.PhysicsBody3D.Sphere(0.5, 1.0)

Zanna.Graphics3D.SceneNode.SetPosition(parent, 5.0, 0.0, 0.0)
Zanna.Graphics3D.SceneNode.AddChild(parent, child)
Zanna.Graphics3D.SceneGraph.Add(scene, parent)

child.BindBody(body)
child.SyncMode = 0
Zanna.Graphics3D.PhysicsBody3D.SetPosition(body, 6.0, 1.5, -2.0)
scene.SyncBindings(0.016)

childPos = child.Position
PRINT "Child local X from body = "; Zanna.Math.Vec3.get_X(childPos)

skel = Zanna.Graphics3D.Skeleton3D.New()
rootBone = Zanna.Graphics3D.Skeleton3D.AddBone(skel, "root", -1, Zanna.Math.Mat4.Identity())
Zanna.Graphics3D.Skeleton3D.ComputeInverseBind(skel)

walk = Zanna.Graphics3D.Animation3D.New("walk", 1.0)
Zanna.Graphics3D.Animation3D.set_Looping(walk, 1)
rot = Zanna.Math.Quat.Identity()
scl = Zanna.Math.Vec3.One()
Zanna.Graphics3D.Animation3D.AddKeyframe(walk, rootBone, 0.0, Zanna.Math.Vec3.Zero(), rot, scl)
Zanna.Graphics3D.Animation3D.AddKeyframe(walk, rootBone, 1.0, Zanna.Math.Vec3.New(4.0, 0.0, 0.0), rot, scl)

controller = Zanna.Graphics3D.AnimController3D.New(skel)
Zanna.Graphics3D.AnimController3D.AddState(controller, "walk", walk)
Zanna.Graphics3D.AnimController3D.SetRootMotionBone(controller, rootBone)
Zanna.Graphics3D.AnimController3D.Play(controller, "walk")
Zanna.Graphics3D.AnimController3D.Update(controller, 0.5)

animNode = Zanna.Graphics3D.SceneNode.New()
animNode.BindAnimator(controller)
animNode.SyncMode = 2
Zanna.Graphics3D.SceneGraph.Add(scene, animNode)
scene.SyncBindings(0.016)

animPos = animNode.Position
PRINT "Animator root motion X = "; Zanna.Math.Vec3.get_X(animPos)
PRINT "Sync mode = "; animNode.SyncMode
