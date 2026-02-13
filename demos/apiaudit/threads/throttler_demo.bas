' throttler_demo.bas
PRINT "=== Viper.Threads.Throttler Demo ==="
DIM t AS OBJECT
t = NEW Viper.Threads.Throttler(100)
PRINT t.Interval
PRINT t.CanProceed
PRINT t.Try()
PRINT t.Count
t.Reset()
PRINT t.Count
PRINT "done"
END
