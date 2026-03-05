' grid2d_demo.bas - Comprehensive API audit for Viper.Game.Grid2D
' Tests: New, Set, Get, Width, Height, Size, InBounds, Fill, Clear,
'        CopyFrom, Count, Replace

PRINT "=== Grid2D API Audit ==="

' --- New (width, height, defaultValue) ---
PRINT "--- New ---"
DIM g AS OBJECT
g = Viper.Game.Grid2D.New(4, 3, 0)
PRINT g.Width      ' 4
PRINT g.Height     ' 3
PRINT g.Size       ' 12 (4 * 3)

' --- Set / Get ---
PRINT "--- Set / Get ---"
g.Set(0, 0, 42)
PRINT g.Get(0, 0)  ' 42
g.Set(3, 2, 99)
PRINT g.Get(3, 2)  ' 99
PRINT g.Get(1, 1)  ' 0 (default value)

' --- InBounds ---
PRINT "--- InBounds ---"
PRINT g.InBounds(0, 0)    ' 1 (top-left corner)
PRINT g.InBounds(3, 2)    ' 1 (bottom-right corner)
PRINT g.InBounds(4, 0)    ' 0 (out of bounds x)
PRINT g.InBounds(0, 3)    ' 0 (out of bounds y)
PRINT g.InBounds(-1, 0)   ' 0 (negative)

' --- Fill ---
PRINT "--- Fill ---"
g.Fill(7)
PRINT g.Get(0, 0)  ' 7
PRINT g.Get(1, 1)  ' 7
PRINT g.Get(3, 2)  ' 7

' --- Clear ---
PRINT "--- Clear ---"
g.Clear()
PRINT g.Get(0, 0)  ' 0
PRINT g.Get(1, 1)  ' 0
PRINT g.Get(3, 2)  ' 0

' --- Count ---
PRINT "--- Count ---"
g.Set(0, 0, 5)
g.Set(1, 0, 5)
g.Set(2, 1, 5)
PRINT g.Count(5)   ' 3
PRINT g.Count(0)   ' 9
PRINT g.Count(99)  ' 0

' --- Replace ---
PRINT "--- Replace ---"
DIM replaced AS INTEGER
replaced = g.Replace(5, 10)
PRINT replaced         ' 3
PRINT g.Get(0, 0)     ' 10
PRINT g.Get(1, 0)     ' 10
PRINT g.Get(2, 1)     ' 10
PRINT g.Count(5)      ' 0
PRINT g.Count(10)     ' 3

' --- CopyFrom ---
PRINT "--- CopyFrom ---"
DIM g2 AS OBJECT
g2 = Viper.Game.Grid2D.New(4, 3, 0)
g2.Fill(55)
DIM ok AS INTEGER
ok = g.CopyFrom(g2)
PRINT ok               ' 1 (same dimensions)
PRINT g.Get(0, 0)     ' 55
PRINT g.Get(3, 2)     ' 55

' --- CopyFrom mismatched size ---
PRINT "--- CopyFrom mismatch ---"
DIM g3 AS OBJECT
g3 = Viper.Game.Grid2D.New(2, 2, 0)
DIM bad AS INTEGER
bad = g.CopyFrom(g3)
PRINT bad              ' 0 (different dimensions)

' --- Width / Height after operations ---
PRINT "--- Width / Height ---"
PRINT g.Width      ' 4
PRINT g.Height     ' 3

PRINT "=== Grid2D audit complete ==="
END
