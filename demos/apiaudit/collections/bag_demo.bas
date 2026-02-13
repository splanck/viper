' bag_demo.bas
PRINT "=== Viper.Collections.Bag Demo ==="
DIM b AS OBJECT
b = NEW Viper.Collections.Bag()
b.Put("apple")
b.Put("apple")
b.Put("banana")
PRINT b.Len
PRINT b.Has("banana")
b.Drop("apple")
PRINT b.Len
b.Clear()
PRINT b.IsEmpty
PRINT "done"
END
