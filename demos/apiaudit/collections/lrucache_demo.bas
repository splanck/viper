' lrucache_demo.bas
PRINT "=== Viper.Collections.LruCache Demo ==="
DIM c AS OBJECT
c = NEW Viper.Collections.LruCache(3)
c.Put("a", "alpha")
c.Put("b", "beta")
c.Put("c", "gamma")
PRINT c.Len
PRINT c.Cap
PRINT c.Has("a")
c.Put("d", "delta")
PRINT c.Len
PRINT c.Has("a")
c.Remove("b")
PRINT c.Len
c.Clear()
PRINT c.IsEmpty
PRINT "done"
END
