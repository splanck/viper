' timer_demo.bas
PRINT "=== Viper.Game.Timer Demo ==="
DIM t AS OBJECT
t = NEW Viper.Game.Timer()
PRINT t.IsRunning
PRINT t.IsExpired
t.Start(100)
PRINT t.IsRunning
PRINT t.Duration
PRINT t.Elapsed
t.Stop()
PRINT t.IsRunning
t.Reset()
PRINT "done"
END
