' =============================================================================
' API Audit: Viper.Graphics.Color (BASIC)
' =============================================================================
' Tests: RGB, RGBA, FromHSL, GetR, GetG, GetB, GetA, GetH, GetS, GetL,
'        Lerp, Brighten, Darken, FromHex, ToHex, Saturate, Desaturate,
'        Complement, Grayscale, Invert
' =============================================================================

PRINT "=== API Audit: Viper.Graphics.Color ==="

' --- RGB ---
PRINT "--- RGB ---"
DIM red AS INTEGER = Viper.Graphics.Color.RGB(255, 0, 0)
PRINT "RGB(255, 0, 0): "; red
DIM green AS INTEGER = Viper.Graphics.Color.RGB(0, 255, 0)
PRINT "RGB(0, 255, 0): "; green
DIM blue AS INTEGER = Viper.Graphics.Color.RGB(0, 0, 255)
PRINT "RGB(0, 0, 255): "; blue
DIM white AS INTEGER = Viper.Graphics.Color.RGB(255, 255, 255)
PRINT "RGB(255, 255, 255): "; white
DIM black AS INTEGER = Viper.Graphics.Color.RGB(0, 0, 0)
PRINT "RGB(0, 0, 0): "; black

' --- RGBA ---
PRINT "--- RGBA ---"
DIM semiRed AS INTEGER = Viper.Graphics.Color.RGBA(255, 0, 0, 128)
PRINT "RGBA(255, 0, 0, 128): "; semiRed
DIM opaqueBlue AS INTEGER = Viper.Graphics.Color.RGBA(0, 0, 255, 255)
PRINT "RGBA(0, 0, 255, 255): "; opaqueBlue
DIM transparent AS INTEGER = Viper.Graphics.Color.RGBA(0, 0, 0, 0)
PRINT "RGBA(0, 0, 0, 0): "; transparent

' --- GetR ---
PRINT "--- GetR ---"
PRINT "GetR(red): "; Viper.Graphics.Color.GetR(red)
PRINT "GetR(green): "; Viper.Graphics.Color.GetR(green)
PRINT "GetR(white): "; Viper.Graphics.Color.GetR(white)

' --- GetG ---
PRINT "--- GetG ---"
PRINT "GetG(red): "; Viper.Graphics.Color.GetG(red)
PRINT "GetG(green): "; Viper.Graphics.Color.GetG(green)
PRINT "GetG(white): "; Viper.Graphics.Color.GetG(white)

' --- GetB ---
PRINT "--- GetB ---"
PRINT "GetB(red): "; Viper.Graphics.Color.GetB(red)
PRINT "GetB(blue): "; Viper.Graphics.Color.GetB(blue)
PRINT "GetB(white): "; Viper.Graphics.Color.GetB(white)

' --- GetA ---
PRINT "--- GetA ---"
PRINT "GetA(semiRed): "; Viper.Graphics.Color.GetA(semiRed)
PRINT "GetA(opaqueBlue): "; Viper.Graphics.Color.GetA(opaqueBlue)
PRINT "GetA(transparent): "; Viper.Graphics.Color.GetA(transparent)

' --- FromHSL ---
PRINT "--- FromHSL ---"
DIM hslRed AS INTEGER = Viper.Graphics.Color.FromHSL(0, 100, 50)
PRINT "FromHSL(0, 100, 50): "; hslRed
DIM hslGreen AS INTEGER = Viper.Graphics.Color.FromHSL(120, 100, 50)
PRINT "FromHSL(120, 100, 50): "; hslGreen
DIM hslBlue AS INTEGER = Viper.Graphics.Color.FromHSL(240, 100, 50)
PRINT "FromHSL(240, 100, 50): "; hslBlue
DIM hslWhite AS INTEGER = Viper.Graphics.Color.FromHSL(0, 0, 100)
PRINT "FromHSL(0, 0, 100): "; hslWhite
DIM hslBlack AS INTEGER = Viper.Graphics.Color.FromHSL(0, 0, 0)
PRINT "FromHSL(0, 0, 0): "; hslBlack

' --- GetH ---
PRINT "--- GetH ---"
PRINT "GetH(red): "; Viper.Graphics.Color.GetH(red)
PRINT "GetH(green): "; Viper.Graphics.Color.GetH(green)
PRINT "GetH(blue): "; Viper.Graphics.Color.GetH(blue)

' --- GetS ---
PRINT "--- GetS ---"
PRINT "GetS(red): "; Viper.Graphics.Color.GetS(red)
PRINT "GetS(white): "; Viper.Graphics.Color.GetS(white)

' --- GetL ---
PRINT "--- GetL ---"
PRINT "GetL(red): "; Viper.Graphics.Color.GetL(red)
PRINT "GetL(white): "; Viper.Graphics.Color.GetL(white)
PRINT "GetL(black): "; Viper.Graphics.Color.GetL(black)

' --- Lerp ---
PRINT "--- Lerp ---"
DIM lerp0 AS INTEGER = Viper.Graphics.Color.Lerp(red, blue, 0)
PRINT "Lerp(red, blue, 0): "; lerp0
DIM lerp50 AS INTEGER = Viper.Graphics.Color.Lerp(red, blue, 50)
PRINT "Lerp(red, blue, 50): "; lerp50
DIM lerp100 AS INTEGER = Viper.Graphics.Color.Lerp(red, blue, 100)
PRINT "Lerp(red, blue, 100): "; lerp100
PRINT "Lerp R at 50%: "; Viper.Graphics.Color.GetR(lerp50)
PRINT "Lerp B at 50%: "; Viper.Graphics.Color.GetB(lerp50)

' --- Brighten ---
PRINT "--- Brighten ---"
DIM brightRed AS INTEGER = Viper.Graphics.Color.Brighten(red, 30)
PRINT "Brighten(red, 30): "; brightRed
DIM brightBlue AS INTEGER = Viper.Graphics.Color.Brighten(blue, 50)
PRINT "Brighten(blue, 50): "; brightBlue

' --- Darken ---
PRINT "--- Darken ---"
DIM darkRed AS INTEGER = Viper.Graphics.Color.Darken(red, 30)
PRINT "Darken(red, 30): "; darkRed
DIM darkWhite AS INTEGER = Viper.Graphics.Color.Darken(white, 50)
PRINT "Darken(white, 50): "; darkWhite

' --- FromHex ---
PRINT "--- FromHex ---"
DIM hexRed AS INTEGER = Viper.Graphics.Color.FromHex("#FF0000")
PRINT "FromHex(#FF0000): "; hexRed
DIM hexGreen AS INTEGER = Viper.Graphics.Color.FromHex("#00FF00")
PRINT "FromHex(#00FF00): "; hexGreen
DIM hexBlue AS INTEGER = Viper.Graphics.Color.FromHex("#0000FF")
PRINT "FromHex(#0000FF): "; hexBlue

' --- ToHex ---
PRINT "--- ToHex ---"
PRINT "ToHex(red): "; Viper.Graphics.Color.ToHex(red)
PRINT "ToHex(green): "; Viper.Graphics.Color.ToHex(green)
PRINT "ToHex(blue): "; Viper.Graphics.Color.ToHex(blue)

' --- Saturate ---
PRINT "--- Saturate ---"
DIM muted AS INTEGER = Viper.Graphics.Color.RGB(128, 100, 100)
DIM saturated AS INTEGER = Viper.Graphics.Color.Saturate(muted, 50)
PRINT "Saturate(muted, 50): "; saturated

' --- Desaturate ---
PRINT "--- Desaturate ---"
DIM desaturated AS INTEGER = Viper.Graphics.Color.Desaturate(red, 50)
PRINT "Desaturate(red, 50): "; desaturated
DIM fullDesat AS INTEGER = Viper.Graphics.Color.Desaturate(red, 100)
PRINT "Desaturate(red, 100): "; fullDesat

' --- Complement ---
PRINT "--- Complement ---"
DIM compRed AS INTEGER = Viper.Graphics.Color.Complement(red)
PRINT "Complement(red): "; compRed
PRINT "Complement R: "; Viper.Graphics.Color.GetR(compRed)
PRINT "Complement G: "; Viper.Graphics.Color.GetG(compRed)
PRINT "Complement B: "; Viper.Graphics.Color.GetB(compRed)

' --- Grayscale ---
PRINT "--- Grayscale ---"
DIM grayRed AS INTEGER = Viper.Graphics.Color.Grayscale(red)
PRINT "Grayscale(red): "; grayRed
DIM grayGreen AS INTEGER = Viper.Graphics.Color.Grayscale(green)
PRINT "Grayscale(green): "; grayGreen
DIM grayWhite AS INTEGER = Viper.Graphics.Color.Grayscale(white)
PRINT "Grayscale(white): "; grayWhite

' --- Invert ---
PRINT "--- Invert ---"
DIM invRed AS INTEGER = Viper.Graphics.Color.Invert(red)
PRINT "Invert(red): "; invRed
PRINT "Invert R: "; Viper.Graphics.Color.GetR(invRed)
PRINT "Invert G: "; Viper.Graphics.Color.GetG(invRed)
PRINT "Invert B: "; Viper.Graphics.Color.GetB(invRed)
DIM invBlack AS INTEGER = Viper.Graphics.Color.Invert(black)
PRINT "Invert(black): "; invBlack
DIM invWhite AS INTEGER = Viper.Graphics.Color.Invert(white)
PRINT "Invert(white): "; invWhite

PRINT "=== Color Audit Complete ==="
END
