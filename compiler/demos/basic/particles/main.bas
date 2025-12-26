' Particle System Demo
' A visually impressive particle fountain with fading

AddFile "particle.bas"
AddFile "system.bas"
AddFile "emitter.bas"

DIM canvas AS Viper.Graphics.Canvas
canvas = NEW Viper.Graphics.Canvas("Particle Demo", 800, 600)

DIM black AS INTEGER
black = Viper.Graphics.Color.RGB(0, 0, 0)

' Create multiple colors
DIM orange AS INTEGER
orange = Viper.Graphics.Color.RGB(255, 150, 50)
DIM yellow AS INTEGER
yellow = Viper.Graphics.Color.RGB(255, 255, 100)
DIM red AS INTEGER
red = Viper.Graphics.Color.RGB(255, 80, 50)

' Create particle system
DIM ps AS ParticleSystem
ps = NEW ParticleSystem(500)

' Create three emitters
DIM em1 AS Emitter
em1 = NEW Emitter(400.0, 550.0, 40, orange)

DIM em2 AS Emitter
em2 = NEW Emitter(200.0, 550.0, 30, yellow)

DIM em3 AS Emitter
em3 = NEW Emitter(600.0, 550.0, 30, red)

' Main loop
WHILE NOT canvas.ShouldClose
    canvas.Clear(black)

    ' Emit particles
    em1.Emit(ps)
    em2.Emit(ps)
    em3.Emit(ps)

    ' Update, remove dead, and draw
    ps.UpdateAll(0.15, 800, 600)
    ps.RemoveDead()
    ps.DrawAll(canvas)

    canvas.Flip()
    canvas.Poll()
    SLEEP 16
WEND
