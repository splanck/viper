' color_demo.bas
PRINT "=== Viper.Graphics.Color Demo ==="
DIM c AS INTEGER
c = Viper.Graphics.Color.RGB(255, 128, 64)
PRINT c
PRINT Viper.Graphics.Color.GetR(c)
PRINT Viper.Graphics.Color.GetG(c)
PRINT Viper.Graphics.Color.GetB(c)
PRINT Viper.Graphics.Color.GetA(c)
DIM hex AS STRING
hex = Viper.Graphics.Color.ToHex(c)
PRINT hex
DIM fromHex AS INTEGER
fromHex = Viper.Graphics.Color.FromHex("#FF8040")
PRINT fromHex
DIM comp AS INTEGER
comp = Viper.Graphics.Color.Complement(c)
PRINT comp
PRINT "done"
END
