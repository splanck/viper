DIM path AS STRING
DIM scene AS OBJECT
DIM parent AS OBJECT
DIM child AS OBJECT
DIM mesh AS OBJECT
DIM mat AS OBJECT
DIM model AS OBJECT
DIM loadResult AS OBJECT
DIM inst AS OBJECT
DIM instScene AS OBJECT
DIM node AS OBJECT
DIM nodeOption AS OBJECT
DIM pos AS OBJECT

PRINT "=== SceneAsset Demo ==="

    path = "scene_asset_demo.vscn"
scene = Viper.Graphics3D.SceneGraph.New()
parent = Viper.Graphics3D.SceneNode.New()
child = Viper.Graphics3D.SceneNode.New()
mesh = Viper.Graphics3D.Mesh3D.Box(1.0, 2.0, 3.0)
mat = Viper.Graphics3D.Material3D.FromColor(0.2, 0.4, 0.8)

Viper.Graphics3D.SceneNode.set_Name(parent, "parent")
Viper.Graphics3D.SceneNode.set_Name(child, "child")
Viper.Graphics3D.SceneNode.SetPosition(parent, 1.0, 2.0, 3.0)
Viper.Graphics3D.SceneNode.SetPosition(child, 0.0, 5.0, 0.0)
parent.Mesh = mesh
parent.Material = mat
child.Mesh = mesh
child.Material = mat
Viper.Graphics3D.SceneNode.AddChild(parent, child)
Viper.Graphics3D.SceneGraph.Add(scene, parent)
Viper.Graphics3D.SceneGraph.Save(scene, path)

loadResult = Viper.Graphics3D.SceneAsset.LoadResult(path)
model = Viper.Result.Unwrap(loadResult)
PRINT "MeshCount = "; Viper.Graphics3D.SceneAsset.get_MeshCount(model)
PRINT "MaterialCount = "; Viper.Graphics3D.SceneAsset.get_MaterialCount(model)
PRINT "NodeCount = "; Viper.Graphics3D.SceneAsset.get_NodeCount(model)

nodeOption = Viper.Graphics3D.SceneAsset.FindNode(model, "child")
node = Viper.Option.Unwrap(nodeOption)
pos = Viper.Graphics3D.SceneNode.get_Position(node)
PRINT "Template child Y = "; Viper.Math.Vec3.get_Y(pos)

inst = Viper.Graphics3D.SceneAsset.Instantiate(model)
nodeOption = Viper.Graphics3D.SceneNode.FindOption(inst, "child")
node = Viper.Option.Unwrap(nodeOption)
Viper.Graphics3D.SceneNode.SetPosition(node, 9.0, 9.0, 9.0)
pos = Viper.Graphics3D.SceneNode.get_Position(node)
PRINT "Instance child Y = "; Viper.Math.Vec3.get_Y(pos)

instScene = Viper.Graphics3D.SceneAsset.InstantiateScene(model)
PRINT "Scene node count = "; Viper.Graphics3D.SceneGraph.get_NodeCount(instScene)
