' EXPECT_OUT: RESULT: ok
' COVER: Viper.Graphics.Color.FromHSL
' COVER: Viper.Graphics.Color.Lerp
' COVER: Viper.Graphics.Color.GetR
' COVER: Viper.Graphics.Color.GetG
' COVER: Viper.Graphics.Color.GetB
' COVER: Viper.Graphics.Color.GetA
' COVER: Viper.Graphics.Color.Brighten
' COVER: Viper.Graphics.Color.Darken
' COVER: Viper.Graphics.Pixels.Invert
' COVER: Viper.Graphics.Pixels.Grayscale
' COVER: Viper.Graphics.Pixels.Tint
' COVER: Viper.Graphics.Pixels.Blur
' COVER: Viper.Graphics.Pixels.Resize
' COVER: Viper.Graphics.Canvas.GradientH
' COVER: Viper.Graphics.Canvas.GradientV
' COVER: Viper.Graphics.Sprite.New
' COVER: Viper.Graphics.Sprite.X
' COVER: Viper.Graphics.Sprite.Y
' COVER: Viper.Graphics.Sprite.Width
' COVER: Viper.Graphics.Sprite.Height
' COVER: Viper.Graphics.Sprite.Visible
' COVER: Viper.Graphics.Sprite.ScaleX
' COVER: Viper.Graphics.Sprite.ScaleY
' COVER: Viper.Graphics.Sprite.Frame
' COVER: Viper.Graphics.Sprite.FrameCount
' COVER: Viper.Graphics.Sprite.Draw
' COVER: Viper.Graphics.Sprite.Move
' COVER: Viper.Graphics.Sprite.Contains
' COVER: Viper.Graphics.Tilemap.New
' COVER: Viper.Graphics.Tilemap.Width
' COVER: Viper.Graphics.Tilemap.Height
' COVER: Viper.Graphics.Tilemap.TileWidth
' COVER: Viper.Graphics.Tilemap.TileHeight
' COVER: Viper.Graphics.Tilemap.SetTile
' COVER: Viper.Graphics.Tilemap.GetTile
' COVER: Viper.Graphics.Tilemap.Fill
' COVER: Viper.Graphics.Tilemap.Clear
' COVER: Viper.Graphics.Camera.New
' COVER: Viper.Graphics.Camera.X
' COVER: Viper.Graphics.Camera.Y
' COVER: Viper.Graphics.Camera.Width
' COVER: Viper.Graphics.Camera.Height
' COVER: Viper.Graphics.Camera.Zoom
' COVER: Viper.Graphics.Camera.Move
' COVER: Viper.Graphics.Camera.ToScreenX
' COVER: Viper.Graphics.Camera.ToScreenY
' COVER: Viper.Graphics.Camera.ToWorldX
' COVER: Viper.Graphics.Camera.ToWorldY

'=============================================================================
' Test Color Extended Methods
'=============================================================================
DIM red AS INTEGER
DIM green AS INTEGER
DIM blue AS INTEGER
DIM white AS INTEGER
red = Viper.Graphics.Color.RGB(255, 0, 0)
green = Viper.Graphics.Color.RGB(0, 255, 0)
blue = Viper.Graphics.Color.RGB(0, 0, 255)
white = Viper.Graphics.Color.RGB(255, 255, 255)

' Test GetR, GetG, GetB, GetA
DIM r AS INTEGER
DIM g AS INTEGER
DIM b AS INTEGER
DIM a AS INTEGER
r = Viper.Graphics.Color.GetR(red)
g = Viper.Graphics.Color.GetG(red)
b = Viper.Graphics.Color.GetB(red)
a = Viper.Graphics.Color.GetA(red)
Viper.Core.Diagnostics.AssertEq(r, 255, "Color.GetR red")
Viper.Core.Diagnostics.AssertEq(g, 0, "Color.GetG red")
Viper.Core.Diagnostics.AssertEq(b, 0, "Color.GetB red")
' RGB() returns 0x00RRGGBB, so alpha is 0
Viper.Core.Diagnostics.AssertEq(a, 0, "Color.GetA RGB red")

' Test GetA with RGBA color
DIM blueWithAlpha AS INTEGER
blueWithAlpha = Viper.Graphics.Color.RGBA(0, 0, 255, 128)
a = Viper.Graphics.Color.GetA(blueWithAlpha)
Viper.Core.Diagnostics.AssertEq(a, 128, "Color.GetA RGBA")

r = Viper.Graphics.Color.GetR(green)
g = Viper.Graphics.Color.GetG(green)
b = Viper.Graphics.Color.GetB(green)
Viper.Core.Diagnostics.AssertEq(r, 0, "Color.GetR green")
Viper.Core.Diagnostics.AssertEq(g, 255, "Color.GetG green")
Viper.Core.Diagnostics.AssertEq(b, 0, "Color.GetB green")

' Test FromHSL - red is at hue 0
DIM hslRed AS INTEGER
hslRed = Viper.Graphics.Color.FromHSL(0, 100, 50)
Viper.Core.Diagnostics.Assert(Viper.Graphics.Color.GetR(hslRed) > 200, "Color.FromHSL red R")
Viper.Core.Diagnostics.Assert(Viper.Graphics.Color.GetG(hslRed) < 50, "Color.FromHSL red G")
Viper.Core.Diagnostics.Assert(Viper.Graphics.Color.GetB(hslRed) < 50, "Color.FromHSL red B")

' Test Lerp
DIM lerped AS INTEGER
lerped = Viper.Graphics.Color.Lerp(red, blue, 50)
Viper.Core.Diagnostics.Assert(Viper.Graphics.Color.GetR(lerped) > 100, "Color.Lerp R mid")
Viper.Core.Diagnostics.Assert(Viper.Graphics.Color.GetB(lerped) > 100, "Color.Lerp B mid")

' Test Brighten and Darken
DIM bright AS INTEGER
DIM dark AS INTEGER
bright = Viper.Graphics.Color.Brighten(red, 50)
dark = Viper.Graphics.Color.Darken(red, 50)
Viper.Core.Diagnostics.Assert(Viper.Graphics.Color.GetR(bright) = 255, "Color.Brighten")
Viper.Core.Diagnostics.Assert(Viper.Graphics.Color.GetR(dark) < 200, "Color.Darken")

'=============================================================================
' Test Pixels Extended Methods
'=============================================================================
DIM pixels AS Viper.Graphics.Pixels
pixels = NEW Viper.Graphics.Pixels(8, 8)
pixels.Fill(red)

' Test Invert
DIM inverted AS Viper.Graphics.Pixels
inverted = pixels.Invert()
Viper.Core.Diagnostics.AssertEq(inverted.Width, 8, "Pixels.Invert width")
Viper.Core.Diagnostics.AssertEq(inverted.Height, 8, "Pixels.Invert height")
' Red (255,0,0) inverted should be cyan (0,255,255)
DIM invColor AS INTEGER
invColor = inverted.Get(0, 0)
Viper.Core.Diagnostics.AssertEq(Viper.Graphics.Color.GetR(invColor), 0, "Pixels.Invert R")
Viper.Core.Diagnostics.AssertEq(Viper.Graphics.Color.GetG(invColor), 255, "Pixels.Invert G")
Viper.Core.Diagnostics.AssertEq(Viper.Graphics.Color.GetB(invColor), 255, "Pixels.Invert B")

' Test Grayscale
DIM gray AS Viper.Graphics.Pixels
gray = pixels.Grayscale()
Viper.Core.Diagnostics.AssertEq(gray.Width, 8, "Pixels.Grayscale width")
DIM grayColor AS INTEGER
grayColor = gray.Get(0, 0)
' Grayscale of red should have R=G=B
DIM grayR AS INTEGER
DIM grayG AS INTEGER
grayR = Viper.Graphics.Color.GetR(grayColor)
grayG = Viper.Graphics.Color.GetG(grayColor)
Viper.Core.Diagnostics.AssertEq(grayR, grayG, "Pixels.Grayscale R=G")

' Test Tint
DIM tinted AS Viper.Graphics.Pixels
DIM tintBlue AS INTEGER
tintBlue = Viper.Graphics.Color.RGBA(0, 0, 255, 128)
tinted = pixels.Tint(tintBlue)
Viper.Core.Diagnostics.AssertEq(tinted.Width, 8, "Pixels.Tint width")

' Test Blur
DIM blurred AS Viper.Graphics.Pixels
blurred = pixels.Blur(1)
Viper.Core.Diagnostics.AssertEq(blurred.Width, 8, "Pixels.Blur width")

' Test Resize
DIM resized AS Viper.Graphics.Pixels
resized = pixels.Resize(16, 16)
Viper.Core.Diagnostics.AssertEq(resized.Width, 16, "Pixels.Resize width")
Viper.Core.Diagnostics.AssertEq(resized.Height, 16, "Pixels.Resize height")

'=============================================================================
' Test Canvas Extended Methods (GradientH, GradientV)
'=============================================================================
DIM canvas AS Viper.Graphics.Canvas
canvas = NEW Viper.Graphics.Canvas("Extended Test", 64, 48)

canvas.Clear(white)
canvas.GradientH(0, 0, 32, 16, red, blue)
canvas.GradientV(32, 0, 32, 16, green, red)
canvas.Flip()

'=============================================================================
' Test Sprite Class
'=============================================================================
DIM spritePixels AS Viper.Graphics.Pixels
spritePixels = NEW Viper.Graphics.Pixels(16, 16)
spritePixels.Fill(red)

DIM sprite AS Viper.Graphics.Sprite
sprite = NEW Viper.Graphics.Sprite(spritePixels)

Viper.Core.Diagnostics.AssertEq(sprite.X, 0, "Sprite.X initial")
Viper.Core.Diagnostics.AssertEq(sprite.Y, 0, "Sprite.Y initial")
Viper.Core.Diagnostics.AssertEq(sprite.Width, 16, "Sprite.Width")
Viper.Core.Diagnostics.AssertEq(sprite.Height, 16, "Sprite.Height")
Viper.Core.Diagnostics.AssertEq(sprite.Visible, 1, "Sprite.Visible initial")
Viper.Core.Diagnostics.AssertEq(sprite.ScaleX, 100, "Sprite.ScaleX initial")
Viper.Core.Diagnostics.AssertEq(sprite.ScaleY, 100, "Sprite.ScaleY initial")
Viper.Core.Diagnostics.AssertEq(sprite.FrameCount, 1, "Sprite.FrameCount")
Viper.Core.Diagnostics.AssertEq(sprite.Frame, 0, "Sprite.Frame initial")

' Test property setters
sprite.X = 10
sprite.Y = 20
Viper.Core.Diagnostics.AssertEq(sprite.X, 10, "Sprite.X set")
Viper.Core.Diagnostics.AssertEq(sprite.Y, 20, "Sprite.Y set")

sprite.ScaleX = 200
sprite.ScaleY = 150
Viper.Core.Diagnostics.AssertEq(sprite.ScaleX, 200, "Sprite.ScaleX set")
Viper.Core.Diagnostics.AssertEq(sprite.ScaleY, 150, "Sprite.ScaleY set")

sprite.Visible = 0
Viper.Core.Diagnostics.AssertEq(sprite.Visible, 0, "Sprite.Visible set")
sprite.Visible = 1

' Test Move
sprite.Move(5, 5)
Viper.Core.Diagnostics.AssertEq(sprite.X, 15, "Sprite.Move X")
Viper.Core.Diagnostics.AssertEq(sprite.Y, 25, "Sprite.Move Y")

' Test Contains
DIM containsResult AS INTEGER
containsResult = sprite.Contains(15, 25)
Viper.Core.Diagnostics.AssertEq(containsResult, 1, "Sprite.Contains inside")

containsResult = sprite.Contains(0, 0)
Viper.Core.Diagnostics.AssertEq(containsResult, 0, "Sprite.Contains outside")

' Test Draw
sprite.Draw(canvas)

'=============================================================================
' Test Tilemap Class
'=============================================================================
DIM tilemap AS Viper.Graphics.Tilemap
tilemap = NEW Viper.Graphics.Tilemap(10, 8, 16, 16)

Viper.Core.Diagnostics.AssertEq(tilemap.Width, 10, "Tilemap.Width")
Viper.Core.Diagnostics.AssertEq(tilemap.Height, 8, "Tilemap.Height")
Viper.Core.Diagnostics.AssertEq(tilemap.TileWidth, 16, "Tilemap.TileWidth")
Viper.Core.Diagnostics.AssertEq(tilemap.TileHeight, 16, "Tilemap.TileHeight")

' Test SetTile and GetTile
tilemap.SetTile(0, 0, 1)
tilemap.SetTile(1, 0, 2)
tilemap.SetTile(2, 0, 3)
Viper.Core.Diagnostics.AssertEq(tilemap.GetTile(0, 0), 1, "Tilemap.GetTile 0,0")
Viper.Core.Diagnostics.AssertEq(tilemap.GetTile(1, 0), 2, "Tilemap.GetTile 1,0")
Viper.Core.Diagnostics.AssertEq(tilemap.GetTile(2, 0), 3, "Tilemap.GetTile 2,0")

' Test Fill
tilemap.Fill(5)
Viper.Core.Diagnostics.AssertEq(tilemap.GetTile(0, 0), 5, "Tilemap.Fill 0,0")
Viper.Core.Diagnostics.AssertEq(tilemap.GetTile(9, 7), 5, "Tilemap.Fill 9,7")

' Test Clear
tilemap.Clear()
Viper.Core.Diagnostics.AssertEq(tilemap.GetTile(0, 0), 0, "Tilemap.Clear 0,0")

'=============================================================================
' Test Camera Class
'=============================================================================
DIM camera AS Viper.Graphics.Camera
camera = NEW Viper.Graphics.Camera(320, 240)

Viper.Core.Diagnostics.AssertEq(camera.Width, 320, "Camera.Width")
Viper.Core.Diagnostics.AssertEq(camera.Height, 240, "Camera.Height")
Viper.Core.Diagnostics.AssertEq(camera.X, 0, "Camera.X initial")
Viper.Core.Diagnostics.AssertEq(camera.Y, 0, "Camera.Y initial")
Viper.Core.Diagnostics.AssertEq(camera.Zoom, 100, "Camera.Zoom initial")

' Test property setters
camera.X = 100
camera.Y = 50
Viper.Core.Diagnostics.AssertEq(camera.X, 100, "Camera.X set")
Viper.Core.Diagnostics.AssertEq(camera.Y, 50, "Camera.Y set")

camera.Zoom = 200
Viper.Core.Diagnostics.AssertEq(camera.Zoom, 200, "Camera.Zoom set")

' Test Move
camera.Move(10, 20)
Viper.Core.Diagnostics.AssertEq(camera.X, 110, "Camera.Move X")
Viper.Core.Diagnostics.AssertEq(camera.Y, 70, "Camera.Move Y")

' Test coordinate transforms
' At zoom 200%, camera at (110, 70), viewport 320x240
' Screen center is at world (110 + 320/4, 70 + 240/4) = (190, 130)
' Screen (0, 0) should be at world (110, 70) approximately
DIM worldX AS INTEGER
DIM worldY AS INTEGER
DIM screenX AS INTEGER
DIM screenY AS INTEGER

worldX = camera.ToWorldX(0)
worldY = camera.ToWorldY(0)
screenX = camera.ToScreenX(110)
screenY = camera.ToScreenY(70)

' At 200% zoom, screen coords transform to world coords
Viper.Core.Diagnostics.Assert(worldX >= 0, "Camera.ToWorldX valid")
Viper.Core.Diagnostics.Assert(worldY >= 0, "Camera.ToWorldY valid")

PRINT "RESULT: ok"
END
