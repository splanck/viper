' barrier_demo.bas
PRINT "=== Viper.Threads.Barrier Demo ==="
DIM b AS OBJECT
b = NEW Viper.Threads.Barrier(3)
PRINT b.Parties
PRINT b.Waiting
b.Reset()
PRINT b.Waiting
PRINT "done"
END
