' debouncer_demo.bas
PRINT "=== Viper.Threads.Debouncer Demo ==="
DIM d AS OBJECT
d = NEW Viper.Threads.Debouncer(100)
PRINT d.Delay
PRINT d.IsReady
PRINT d.SignalCount
d.Signal()
PRINT d.SignalCount
d.Reset()
PRINT d.SignalCount
PRINT "done"
END
