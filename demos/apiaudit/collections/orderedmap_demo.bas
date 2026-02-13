' orderedmap_demo.bas
PRINT "=== Viper.Collections.OrderedMap Demo ==="
DIM m AS OBJECT
m = NEW Viper.Collections.OrderedMap()
m.Set("c", "gamma")
m.Set("a", "alpha")
m.Set("b", "beta")
PRINT m.Len
PRINT m.Has("a")
PRINT m.KeyAt(0)
PRINT m.KeyAt(1)
m.Remove("a")
PRINT m.Len
m.Clear()
PRINT m.IsEmpty
PRINT "done"
END
