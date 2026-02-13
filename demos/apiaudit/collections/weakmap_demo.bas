' weakmap_demo.bas
PRINT "=== Viper.Collections.WeakMap Demo ==="
DIM w AS OBJECT
w = NEW Viper.Collections.WeakMap()
w.Set("a", "alpha")
w.Set("b", "beta")
PRINT w.Len
PRINT w.Has("a")
PRINT w.Has("z")
w.Remove("a")
PRINT w.Len
w.Clear()
PRINT w.IsEmpty
PRINT "done"
END
