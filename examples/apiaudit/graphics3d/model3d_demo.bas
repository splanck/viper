DIM path AS STRING
DIM scene AS OBJECT
DIM parent AS OBJECT
DIM child AS OBJECT
DIM mesh AS OBJECT
DIM mat AS OBJECT
DIM model AS OBJECT
DIM inst AS OBJECT
DIM instScene AS OBJECT
DIM node AS OBJECT
DIM pos AS OBJECT

PRINT "=== Model3D Demo ==="

path = "model3d_demo.vscn"
scene = Viper.Graphics3D.Scene3D.New()
parent = Viper.Graphics3D.SceneNode3D.New()
child = Viper.Graphics3D.SceneNode3D.New()
mesh = Viper.Graphics3D.Mesh3D.NewBox(1.0, 2.0, 3.0)
mat = Viper.Graphics3D.Material3D.NewColor(0.2, 0.4, 0.8)

Viper.Graphics3D.SceneNode3D.set_Name(parent, "parent")
Viper.Graphics3D.SceneNode3D.set_Name(child, "child")
Viper.Graphics3D.SceneNode3D.SetPosition(parent, 1.0, 2.0, 3.0)
Viper.Graphics3D.SceneNode3D.SetPosition(child, 0.0, 5.0, 0.0)
parent.Mesh = mesh
parent.Material = mat
child.Mesh = mesh
child.Material = mat
Viper.Graphics3D.SceneNode3D.AddChild(parent, child)
Viper.Graphics3D.Scene3D.Add(scene, parent)
Viper.Graphics3D.Scene3D.Save(scene, path)

model = Viper.Graphics3D.Model3D.Load(path)
PRINT "MeshCount = "; model.MeshCount
PRINT "MaterialCount = "; model.MaterialCount
PRINT "NodeCount = "; model.NodeCount

node = model.FindNode("child")
pos = Viper.Graphics3D.SceneNode3D.get_Position(node)
PRINT "Template child Y = "; Viper.Math.Vec3.get_Y(pos)

inst = model.Instantiate()
node = Viper.Graphics3D.SceneNode3D.Find(inst, "child")
Viper.Graphics3D.SceneNode3D.SetPosition(node, 9.0, 9.0, 9.0)
pos = Viper.Graphics3D.SceneNode3D.get_Position(node)
PRINT "Instance child Y = "; Viper.Math.Vec3.get_Y(pos)

instScene = model.InstantiateScene()
PRINT "Scene node count = "; Viper.Graphics3D.Scene3D.get_NodeCount(instScene)
