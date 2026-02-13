' concmap_demo.bas
PRINT "=== Viper.Threads.ConcurrentMap Demo ==="
DIM m AS OBJECT
m = NEW Viper.Threads.ConcurrentMap()
m.Set("a", "alpha")
m.Set("b", "beta")
PRINT m.Len
PRINT m.Has("a")
PRINT m.Has("z")
m.Remove("a")
PRINT m.Len
m.Clear()
PRINT m.IsEmpty
PRINT "done"
END
