PRINT "=== Sound3D Objects Demo ==="

DIM cam AS OBJECT
DIM scene AS OBJECT
DIM parent AS OBJECT
DIM node AS OBJECT
DIM listener AS OBJECT
DIM source AS OBJECT
DIM pos AS OBJECT
DIM voice AS INTEGER

cam = Zanna.Graphics3D.Camera3D.New(60.0, 1.0, 0.1, 100.0)
Zanna.Graphics3D.Camera3D.LookAt(cam, Zanna.Math.Vec3.New(0.0, 2.0, 6.0), Zanna.Math.Vec3.New(0.0, 1.5, 0.0), Zanna.Math.Vec3.New(0.0, 1.0, 0.0))

scene = Zanna.Graphics3D.SceneGraph.New()
parent = Zanna.Graphics3D.SceneNode.New()
node = Zanna.Graphics3D.SceneNode.New()
Zanna.Graphics3D.SceneNode.SetPosition(parent, 1.0, 0.0, 2.0)
Zanna.Graphics3D.SceneNode.SetPosition(node, 3.0, 0.5, -1.0)
Zanna.Graphics3D.SceneNode.AddChild(parent, node)
Zanna.Graphics3D.SceneGraph.Add(scene, parent)

listener = Zanna.Graphics3D.SoundListener3D.New()
listener.BindCamera(cam)
listener.IsActive = 1

source = Zanna.Graphics3D.SoundSource3D.New(Zanna.Audio.Synth.Tone(523, 220, 0))
source.BindNode(node)
source.MaxDistance = 20.0
source.Volume = 75

Zanna.Graphics3D.SceneGraph.SyncBindings(scene, 0.25)

pos = listener.Position
PRINT "Listener Z = "; Zanna.Math.Vec3.get_Z(pos)
pos = source.Position
PRINT "Source X = "; Zanna.Math.Vec3.get_X(pos)
PRINT "Source Z = "; Zanna.Math.Vec3.get_Z(pos)

IF Zanna.Audio.Mixer.IsAvailable() THEN
    IF Zanna.Audio.Mixer.Init() <> 0 THEN
        voice = source.Play()
        PRINT "VoiceId = "; voice
        PRINT "IsPlaying = "; source.IsPlaying
        source.Stop()
    ELSE
        PRINT "Audio init failed on this machine"
    END IF
ELSE
    PRINT "Audio backend unavailable on this machine"
END IF
