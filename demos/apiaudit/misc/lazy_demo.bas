' lazy_demo.bas
PRINT "=== Viper.Lazy Demo ==="
DIM lz AS OBJECT
lz = Viper.Lazy.OfI64(42)
PRINT lz.IsEvaluated
PRINT lz.GetI64()
PRINT lz.IsEvaluated
DIM ls AS OBJECT
ls = Viper.Lazy.OfStr("hello")
PRINT ls.GetStr()
PRINT "done"
END
