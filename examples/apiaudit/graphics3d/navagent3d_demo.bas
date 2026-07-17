PRINT "NavAgent3D demo"

DIM mesh AS OBJECT
DIM nav AS OBJECT
DIM agent AS OBJECT
DIM node AS OBJECT
DIM pos AS OBJECT
DIM world AS OBJECT
DIM character AS OBJECT
DIM i AS INTEGER

mesh = Zanna.Graphics3D.Mesh3D.Plane(20.0, 20.0)
nav = Zanna.Graphics3D.NavMesh3D.Build(mesh, 0.4, 1.8)
agent = Zanna.Graphics3D.NavAgent3D.New(nav, 0.4, 1.8)
node = Zanna.Graphics3D.SceneNode.New()

agent.BindNode(node)
agent.DesiredSpeed = 5.0
agent.Warp(Zanna.Math.Vec3.New(0.0, 0.0, 0.0))
agent.SetTarget(Zanna.Math.Vec3.New(4.0, 0.0, 3.0))

FOR i = 1 TO 20
    agent.Update(0.1)
NEXT i

pos = agent.Position
PRINT "Node path X = "; Zanna.Math.Vec3.get_X(pos)
PRINT "Node path Z = "; Zanna.Math.Vec3.get_Z(pos)
PRINT "RemainingDistance = "; agent.RemainingDistance

world = Zanna.Graphics3D.PhysicsWorld3D.New(0.0, -9.8, 0.0)
character = Zanna.Graphics3D.Character3D.New(0.4, 1.8, 80.0)
character.World = world
agent.BindCharacter(character)
agent.SetTarget(Zanna.Math.Vec3.New(2.0, 0.0, -2.0))

FOR i = 1 TO 15
    agent.Update(0.1)
NEXT i

pos = character.Position
PRINT "Character X = "; Zanna.Math.Vec3.get_X(pos)
PRINT "Character Z = "; Zanna.Math.Vec3.get_Z(pos)

agent.Warp(Zanna.Math.Vec3.New(-3.0, 0.0, 0.0))
pos = agent.Position
PRINT "Warped X = "; Zanna.Math.Vec3.get_X(pos)
PRINT "HasPath = "; agent.HasPath
