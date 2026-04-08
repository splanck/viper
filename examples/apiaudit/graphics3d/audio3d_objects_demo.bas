PRINT "=== Audio3D Objects Demo ==="

DIM cam AS OBJECT
DIM scene AS OBJECT
DIM parent AS OBJECT
DIM node AS OBJECT
DIM listener AS OBJECT
DIM source AS OBJECT
DIM pos AS OBJECT
DIM voice AS INTEGER

cam = Viper.Graphics3D.Camera3D.New(60.0, 1.0, 0.1, 100.0)
Viper.Graphics3D.Camera3D.LookAt(cam, Viper.Math.Vec3.New(0.0, 2.0, 6.0), Viper.Math.Vec3.New(0.0, 1.5, 0.0), Viper.Math.Vec3.New(0.0, 1.0, 0.0))

scene = Viper.Graphics3D.Scene3D.New()
parent = Viper.Graphics3D.SceneNode3D.New()
node = Viper.Graphics3D.SceneNode3D.New()
Viper.Graphics3D.SceneNode3D.SetPosition(parent, 1.0, 0.0, 2.0)
Viper.Graphics3D.SceneNode3D.SetPosition(node, 3.0, 0.5, -1.0)
Viper.Graphics3D.SceneNode3D.AddChild(parent, node)
Viper.Graphics3D.Scene3D.Add(scene, parent)

listener = Viper.Graphics3D.AudioListener3D.New()
listener.BindCamera(cam)
listener.IsActive = 1

source = Viper.Graphics3D.AudioSource3D.New(Viper.Sound.Synth.Tone(523, 220, 80))
source.BindNode(node)
source.MaxDistance = 20.0
source.Volume = 75

Viper.Graphics3D.Scene3D.SyncBindings(scene, 0.25)

pos = listener.Position
PRINT "Listener Z = "; Viper.Math.Vec3.get_Z(pos)
pos = source.Position
PRINT "Source X = "; Viper.Math.Vec3.get_X(pos)
PRINT "Source Z = "; Viper.Math.Vec3.get_Z(pos)

IF Viper.Sound.Audio.IsAvailable() THEN
    IF Viper.Sound.Audio.Init() <> 0 THEN
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
