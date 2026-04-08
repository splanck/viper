PRINT "NavAgent3D demo"

DIM mesh AS OBJECT
DIM nav AS OBJECT
DIM agent AS OBJECT
DIM node AS OBJECT
DIM pos AS OBJECT
DIM world AS OBJECT
DIM character AS OBJECT
DIM i AS INTEGER

mesh = Viper.Graphics3D.Mesh3D.NewPlane(20.0, 20.0)
nav = Viper.Graphics3D.NavMesh3D.Build(mesh, 0.4, 1.8)
agent = Viper.Graphics3D.NavAgent3D.New(nav, 0.4, 1.8)
node = Viper.Graphics3D.SceneNode3D.New()

agent.BindNode(node)
agent.DesiredSpeed = 5.0
agent.Warp(Viper.Math.Vec3.New(0.0, 0.0, 0.0))
agent.SetTarget(Viper.Math.Vec3.New(4.0, 0.0, 3.0))

FOR i = 1 TO 20
    agent.Update(0.1)
NEXT i

pos = agent.Position
PRINT "Node path X = "; Viper.Math.Vec3.get_X(pos)
PRINT "Node path Z = "; Viper.Math.Vec3.get_Z(pos)
PRINT "RemainingDistance = "; agent.RemainingDistance

world = Viper.Graphics3D.Physics3DWorld.New(0.0, -9.8, 0.0)
character = Viper.Graphics3D.Character3D.New(0.4, 1.8, 80.0)
character.World = world
agent.BindCharacter(character)
agent.SetTarget(Viper.Math.Vec3.New(2.0, 0.0, -2.0))

FOR i = 1 TO 15
    agent.Update(0.1)
NEXT i

pos = character.Position
PRINT "Character X = "; Viper.Math.Vec3.get_X(pos)
PRINT "Character Z = "; Viper.Math.Vec3.get_Z(pos)

agent.Warp(Viper.Math.Vec3.New(-3.0, 0.0, 0.0))
pos = agent.Position
PRINT "Warped X = "; Viper.Math.Vec3.get_X(pos)
PRINT "HasPath = "; agent.HasPath
