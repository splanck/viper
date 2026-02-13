' grid2d_demo.bas
PRINT "=== Viper.Game.Grid2D Demo ==="
DIM g AS OBJECT
g = NEW Viper.Game.Grid2D(5, 5, 0)
PRINT g.Width
PRINT g.Height
PRINT g.Size
g.Set(2, 3, 42)
PRINT g.Get(2, 3)
PRINT g.InBounds(4, 4)
PRINT g.InBounds(5, 5)
g.Fill(7)
PRINT g.Count(7)
g.Clear()
PRINT g.Get(2, 3)
PRINT "done"
END
