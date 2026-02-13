' safei64_demo.bas
PRINT "=== Viper.Threads.SafeI64 Demo ==="
DIM a AS OBJECT
a = NEW Viper.Threads.SafeI64(0)
PRINT a.Get()
a.Set(42)
PRINT a.Get()
PRINT a.Add(8)
PRINT a.Get()
PRINT "done"
END
