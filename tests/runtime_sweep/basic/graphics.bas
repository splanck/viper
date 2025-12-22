' EXPECT_OUT: RESULT: ok
' COVER: Viper.Graphics.Canvas.New
' COVER: Viper.Graphics.Canvas.Height
' COVER: Viper.Graphics.Canvas.ShouldClose
' COVER: Viper.Graphics.Canvas.Width
' COVER: Viper.Graphics.Canvas.Box
' COVER: Viper.Graphics.Canvas.Clear
' COVER: Viper.Graphics.Canvas.Disc
' COVER: Viper.Graphics.Canvas.Flip
' COVER: Viper.Graphics.Canvas.Frame
' COVER: Viper.Graphics.Canvas.KeyHeld
' COVER: Viper.Graphics.Canvas.Line
' COVER: Viper.Graphics.Canvas.Plot
' COVER: Viper.Graphics.Canvas.Poll
' COVER: Viper.Graphics.Canvas.Ring
' COVER: Viper.Graphics.Color.RGB
' COVER: Viper.Graphics.Color.RGBA
' COVER: Viper.Graphics.Pixels.New
' COVER: Viper.Graphics.Pixels.Height
' COVER: Viper.Graphics.Pixels.Width
' COVER: Viper.Graphics.Pixels.Clear
' COVER: Viper.Graphics.Pixels.Clone
' COVER: Viper.Graphics.Pixels.Copy
' COVER: Viper.Graphics.Pixels.Fill
' COVER: Viper.Graphics.Pixels.Get
' COVER: Viper.Graphics.Pixels.Set
' COVER: Viper.Graphics.Pixels.ToBytes

DIM canvas AS Viper.Graphics.Canvas
canvas = NEW Viper.Graphics.Canvas("Runtime Canvas", 64, 48)

Viper.Diagnostics.AssertEq(canvas.Width, 64, "canvas.width")
Viper.Diagnostics.AssertEq(canvas.Height, 48, "canvas.height")
Viper.Diagnostics.Assert(canvas.ShouldClose = 0 OR canvas.ShouldClose = 1, "canvas.shouldclose")

DIM red AS INTEGER
DIM green AS INTEGER
DIM blue AS INTEGER
DIM white AS INTEGER
red = Viper.Graphics.Color.RGB(255, 0, 0)
green = Viper.Graphics.Color.RGB(0, 255, 0)
blue = Viper.Graphics.Color.RGBA(0, 0, 255, 255)
white = Viper.Graphics.Color.RGB(255, 255, 255)

canvas.Clear(white)
canvas.Box(2, 2, 10, 8, red)
canvas.Frame(1, 1, 12, 10, blue)
canvas.Line(0, 0, 20, 15, green)
canvas.Disc(20, 20, 4, red)
canvas.Ring(30, 15, 5, blue)
canvas.Plot(5, 5, green)

DIM evt AS INTEGER
evt = canvas.Poll()
Viper.Diagnostics.Assert(evt >= 0, "canvas.poll")

DIM held AS INTEGER
held = canvas.KeyHeld(Viper.Input.Keyboard.KEY_A)
Viper.Diagnostics.Assert(held = 0 OR held = 1, "canvas.keyheld")

canvas.Flip()

DIM pixels AS Viper.Graphics.Pixels
pixels = NEW Viper.Graphics.Pixels(4, 3)
Viper.Diagnostics.AssertEq(pixels.Width, 4, "pixels.width")
Viper.Diagnostics.AssertEq(pixels.Height, 3, "pixels.height")

pixels.Fill(red)
Viper.Diagnostics.AssertEq(pixels.Get(0, 0), red, "pixels.fill")

pixels.Set(1, 1, blue)
Viper.Diagnostics.AssertEq(pixels.Get(1, 1), blue, "pixels.set")

DIM src AS Viper.Graphics.Pixels
src = NEW Viper.Graphics.Pixels(2, 2)
src.Fill(green)

pixels.Copy(2, 0, src, 0, 0, 2, 2)
Viper.Diagnostics.AssertEq(pixels.Get(2, 0), green, "pixels.copy")

DIM clone AS Viper.Graphics.Pixels
clone = pixels.Clone()
Viper.Diagnostics.AssertEq(clone.Get(1, 1), blue, "pixels.clone")

pixels.Clear()
Viper.Diagnostics.AssertEq(pixels.Get(0, 0), 0, "pixels.clear")

DIM buf AS Viper.Collections.Bytes
buf = clone.ToBytes()
Viper.Diagnostics.AssertEq(buf.Len, 4 * 3 * 4, "pixels.tobytes")

PRINT "RESULT: ok"
END
