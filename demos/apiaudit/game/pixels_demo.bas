' pixels_demo.bas
PRINT "=== Viper.Graphics.Pixels Demo ==="
DIM px AS OBJECT
px = NEW Viper.Graphics.Pixels(10, 10)
PRINT px.Width
PRINT px.Height
px.Set(5, 5, 16711680)
PRINT px.Get(5, 5)
px.Fill(255)
PRINT px.Get(0, 0)
px.Clear()
PRINT px.Get(0, 0)
PRINT "done"
END
