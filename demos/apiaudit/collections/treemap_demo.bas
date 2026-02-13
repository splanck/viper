' treemap_demo.bas
PRINT "=== Viper.Collections.TreeMap Demo ==="
DIM t AS OBJECT
t = NEW Viper.Collections.TreeMap()
t.Set("c", "gamma")
t.Set("a", "alpha")
t.Set("b", "beta")
PRINT t.Len
PRINT t.Has("a")
PRINT t.Has("z")
t.Drop("b")
PRINT t.Len
t.Clear()
PRINT t.IsEmpty
PRINT "done"
END
