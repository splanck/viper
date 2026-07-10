' test_color.bas — Viper.Graphics.Color (no window needed)
DIM c AS INTEGER
LET c = Viper.Graphics.Color.RGB(255, 128, 0)
PRINT Viper.Graphics.Color.GetR(c)
PRINT Viper.Graphics.Color.GetG(c)
PRINT Viper.Graphics.Color.GetB(c)
PRINT Viper.Graphics.Color.GetA(c)
PRINT Viper.Graphics.Color.ToHex(c)

DIM c2 AS INTEGER
LET c2 = Viper.Graphics.Color.FromHex("#ff8000")
PRINT Viper.Graphics.Color.GetR(c2)

DIM c3 AS INTEGER
LET c3 = Viper.Graphics.Color.RGBA(100, 200, 50, 128)
PRINT Viper.Graphics.Color.GetA(c3)

DIM c4 AS INTEGER
LET c4 = Viper.Graphics.Color.Grayscale(c)
PRINT Viper.Graphics.Color.GetR(c4)

DIM c5 AS INTEGER
LET c5 = Viper.Graphics.Color.Invert(Viper.Graphics.Color.RGB(0, 0, 0))
PRINT Viper.Graphics.Color.GetR(c5)

DIM c6 AS INTEGER
LET c6 = Viper.Graphics.Color.Complement(c)
PRINT Viper.Graphics.Color.GetR(c6)

DIM c7 AS INTEGER
LET c7 = Viper.Graphics.Color.Lerp(Viper.Graphics.Color.RGB(0,0,0), Viper.Graphics.Color.RGB(255,255,255), 50)
PRINT Viper.Graphics.Color.GetR(c7)

DIM c8 AS INTEGER
LET c8 = Viper.Graphics.Color.Brighten(c, 50)
PRINT Viper.Graphics.Color.GetR(c8)

DIM c9 AS INTEGER
LET c9 = Viper.Graphics.Color.Darken(c, 50)
PRINT Viper.Graphics.Color.GetR(c9)

PRINT "done"
END
