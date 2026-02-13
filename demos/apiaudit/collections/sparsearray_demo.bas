' sparsearray_demo.bas
PRINT "=== Viper.Collections.SparseArray Demo ==="
DIM s AS OBJECT
s = NEW Viper.Collections.SparseArray()
s.Set(0, "zero")
s.Set(100, "hundred")
s.Set(999, "big")
PRINT s.Len
PRINT s.Has(100)
PRINT s.Has(50)
s.Remove(100)
PRINT s.Len
s.Clear()
PRINT s.Len
PRINT "done"
END
