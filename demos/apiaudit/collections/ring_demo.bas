' ring_demo.bas
PRINT "=== Viper.Collections.Ring Demo ==="
DIM r AS OBJECT
r = NEW Viper.Collections.Ring(10)
r.Push("a")
r.Push("b")
r.Push("c")
PRINT r.Len
PRINT r.Cap
PRINT r.IsEmpty
r.Clear()
PRINT r.IsEmpty
PRINT "done"
END
