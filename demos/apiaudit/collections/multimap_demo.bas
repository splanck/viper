' multimap_demo.bas
PRINT "=== Viper.Collections.MultiMap Demo ==="
DIM m AS OBJECT
m = NEW Viper.Collections.MultiMap()
m.Put("fruit", "apple")
m.Put("fruit", "banana")
m.Put("color", "red")
PRINT m.Len
PRINT m.KeyCount
PRINT m.CountFor("fruit")
PRINT m.Has("fruit")
m.RemoveAll("fruit")
PRINT m.Len
m.Clear()
PRINT m.IsEmpty
PRINT "done"
END
