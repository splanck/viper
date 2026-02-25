' =============================================================================
' API Audit: Viper.Graphics.Pixels (BASIC) - headless operations
' =============================================================================
' Tests: New, Get, Set, Width, Height, Fill, Clone, Clear, FlipH, FlipV,
'        RotateCW, RotateCCW, Rotate180, Scale, Invert, Grayscale, Tint,
'        Blur, Resize, ToBytes, FromBytes
' =============================================================================

PRINT "=== API Audit: Viper.Graphics.Pixels ==="

' --- New ---
PRINT "--- New ---"
DIM p AS OBJECT = Viper.Graphics.Pixels.New(32, 16)
PRINT "Created Pixels 32x16"

' --- Width ---
PRINT "--- Width ---"
PRINT "Width: "; p.Width

' --- Height ---
PRINT "--- Height ---"
PRINT "Height: "; p.Height

' --- Get (initial) ---
PRINT "--- Get (initial) ---"
DIM initColor AS INTEGER = p.Get(0, 0)
PRINT "Get(0,0): "; initColor

' --- Set ---
PRINT "--- Set ---"
DIM red AS INTEGER = Viper.Graphics.Color.RGB(255, 0, 0)
p.Set(0, 0, red)
PRINT "Set(0, 0, red)"
DIM readBack AS INTEGER = p.Get(0, 0)
PRINT "Get(0,0) after Set: "; readBack

' --- Set multiple ---
PRINT "--- Set multiple ---"
DIM green AS INTEGER = Viper.Graphics.Color.RGB(0, 255, 0)
DIM blue AS INTEGER = Viper.Graphics.Color.RGB(0, 0, 255)
p.Set(1, 0, green)
p.Set(2, 0, blue)
PRINT "Get(1,0): "; p.Get(1, 0)
PRINT "Get(2,0): "; p.Get(2, 0)

' --- Get (out of bounds) ---
PRINT "--- Get (out of bounds) ---"
DIM oob AS INTEGER = p.Get(100, 100)
PRINT "Get(100,100): "; oob

' --- Fill ---
PRINT "--- Fill ---"
DIM yellow AS INTEGER = Viper.Graphics.Color.RGB(255, 255, 0)
p.Fill(yellow)
PRINT "Filled with yellow"
PRINT "Get(0,0) after Fill: "; p.Get(0, 0)
PRINT "Get(15,7) after Fill: "; p.Get(15, 7)

' --- Clear ---
PRINT "--- Clear ---"
p.Clear()
PRINT "Get(0,0) after Clear: "; p.Get(0, 0)

' --- Clone ---
PRINT "--- Clone ---"
p.Set(5, 5, red)
DIM c AS OBJECT = p.Clone()
PRINT "Clone Width: "; c.Width
PRINT "Clone Height: "; c.Height
PRINT "Clone Get(5,5): "; c.Get(5, 5)
PRINT "Clone Get(0,0): "; c.Get(0, 0)

' --- FlipH ---
PRINT "--- FlipH ---"
DIM src AS OBJECT = Viper.Graphics.Pixels.New(4, 2)
src.Set(0, 0, red)
src.Set(3, 0, blue)
DIM fh AS OBJECT = src.Clone()
fh.FlipH()
PRINT "FlipH Width: "; fh.Width
PRINT "FlipH(0,0): "; fh.Get(0, 0)
PRINT "FlipH(3,0): "; fh.Get(3, 0)

' --- FlipV ---
PRINT "--- FlipV ---"
DIM src2 AS OBJECT = Viper.Graphics.Pixels.New(2, 4)
src2.Set(0, 0, red)
src2.Set(0, 3, green)
DIM fv AS OBJECT = src2.Clone()
fv.FlipV()
PRINT "FlipV Height: "; fv.Height
PRINT "FlipV(0,0): "; fv.Get(0, 0)
PRINT "FlipV(0,3): "; fv.Get(0, 3)

' --- RotateCW ---
PRINT "--- RotateCW ---"
DIM sq AS OBJECT = Viper.Graphics.Pixels.New(4, 2)
sq.Set(0, 0, red)
DIM cw AS OBJECT = sq.RotateCW()
PRINT "RotateCW Width: "; cw.Width
PRINT "RotateCW Height: "; cw.Height

' --- RotateCCW ---
PRINT "--- RotateCCW ---"
DIM ccw AS OBJECT = sq.RotateCCW()
PRINT "RotateCCW Width: "; ccw.Width
PRINT "RotateCCW Height: "; ccw.Height

' --- Rotate180 ---
PRINT "--- Rotate180 ---"
DIM r180 AS OBJECT = sq.Rotate180()
PRINT "Rotate180 Width: "; r180.Width
PRINT "Rotate180 Height: "; r180.Height

' --- Scale ---
PRINT "--- Scale ---"
DIM small AS OBJECT = Viper.Graphics.Pixels.New(8, 8)
small.Fill(red)
DIM scaled AS OBJECT = small.Scale(16, 16)
PRINT "Scale(16,16) Width: "; scaled.Width
PRINT "Scale(16,16) Height: "; scaled.Height
PRINT "Scale pixel(0,0): "; scaled.Get(0, 0)

' --- Resize (bilinear) ---
PRINT "--- Resize ---"
DIM resized AS OBJECT = small.Resize(4, 4)
PRINT "Resize(4,4) Width: "; resized.Width
PRINT "Resize(4,4) Height: "; resized.Height

' --- Invert ---
PRINT "--- Invert ---"
DIM redImg AS OBJECT = Viper.Graphics.Pixels.New(2, 2)
redImg.Fill(red)
DIM inverted AS OBJECT = redImg.Invert()
PRINT "Invert Width: "; inverted.Width
DIM invPixel AS INTEGER = inverted.Get(0, 0)
PRINT "Inverted pixel(0,0): "; invPixel

' --- Grayscale ---
PRINT "--- Grayscale ---"
DIM colorImg AS OBJECT = Viper.Graphics.Pixels.New(2, 2)
colorImg.Fill(red)
DIM gray AS OBJECT = colorImg.Grayscale()
PRINT "Grayscale Width: "; gray.Width
DIM grayPixel AS INTEGER = gray.Get(0, 0)
PRINT "Grayscale pixel(0,0): "; grayPixel

' --- Tint ---
PRINT "--- Tint ---"
DIM whiteImg AS OBJECT = Viper.Graphics.Pixels.New(2, 2)
whiteImg.Fill(Viper.Graphics.Color.RGB(255, 255, 255))
DIM tinted AS OBJECT = whiteImg.Tint(Viper.Graphics.Color.RGB(255, 0, 0))
PRINT "Tint Width: "; tinted.Width
DIM tintPixel AS INTEGER = tinted.Get(0, 0)
PRINT "Tinted pixel(0,0): "; tintPixel

' --- Blur ---
PRINT "--- Blur ---"
DIM blurSrc AS OBJECT = Viper.Graphics.Pixels.New(8, 8)
blurSrc.Fill(Viper.Graphics.Color.RGB(128, 128, 128))
blurSrc.Set(4, 4, Viper.Graphics.Color.RGB(255, 255, 255))
DIM blurred AS OBJECT = blurSrc.Blur(1)
PRINT "Blur Width: "; blurred.Width
PRINT "Blur Height: "; blurred.Height

' --- ToBytes ---
PRINT "--- ToBytes ---"
DIM tiny AS OBJECT = Viper.Graphics.Pixels.New(2, 2)
tiny.Fill(Viper.Graphics.Color.RGB(255, 0, 0))
DIM bytes AS OBJECT = tiny.ToBytes()
PRINT "ToBytes returned object"

' --- FromBytes ---
PRINT "--- FromBytes ---"
DIM restored AS OBJECT = Viper.Graphics.Pixels.FromBytes(2, 2, bytes)
PRINT "FromBytes Width: "; restored.Width
PRINT "FromBytes Height: "; restored.Height
PRINT "FromBytes pixel(0,0): "; restored.Get(0, 0)

PRINT "=== Pixels Audit Complete ==="
END
