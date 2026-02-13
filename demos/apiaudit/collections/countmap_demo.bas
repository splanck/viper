' countmap_demo.bas
PRINT "=== Viper.Collections.CountMap Demo ==="
DIM c AS OBJECT
c = NEW Viper.Collections.CountMap()
c.Inc("apple")
c.Inc("apple")
c.Inc("banana")
PRINT c.Len
PRINT c.Total
PRINT c.Get("apple")
PRINT c.Has("banana")
c.Dec("apple")
PRINT c.Get("apple")
c.Remove("banana")
PRINT c.Len
c.Clear()
PRINT c.IsEmpty
PRINT "done"
END
