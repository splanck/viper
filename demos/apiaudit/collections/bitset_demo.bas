' bitset_demo.bas
PRINT "=== Viper.Collections.BitSet Demo ==="
DIM bs AS OBJECT
bs = NEW Viper.Collections.BitSet(64)
bs.Set(0)
bs.Set(5)
bs.Set(63)
PRINT bs.Count
PRINT bs.Get(5)
PRINT bs.Get(10)
bs.Clear(5)
PRINT bs.Count
bs.Toggle(10)
PRINT bs.Get(10)
PRINT bs.Len
PRINT bs.IsEmpty
bs.ClearAll()
PRINT bs.IsEmpty
PRINT "done"
END
