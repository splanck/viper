' bytes_demo.bas
PRINT "=== Viper.Collections.Bytes Demo ==="
DIM b AS OBJECT
b = NEW Viper.Collections.Bytes(8)
PRINT b.Len
b.Set(0, 72)
b.Set(1, 101)
b.Set(2, 108)
b.Set(3, 108)
b.Set(4, 111)
PRINT b.Get(0)
PRINT b.Find(108)
PRINT b.ToHex()
b.Fill(0)
PRINT b.Get(0)
PRINT "done"
END
