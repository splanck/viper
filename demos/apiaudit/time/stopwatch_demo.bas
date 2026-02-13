' stopwatch_demo.bas
PRINT "=== Viper.Time.Stopwatch Demo ==="
DIM sw AS OBJECT
sw = NEW Viper.Time.Stopwatch()
PRINT sw.IsRunning
sw.Start()
PRINT sw.IsRunning
sw.Stop()
PRINT sw.IsRunning
sw.Reset()
PRINT sw.ElapsedMs
PRINT "done"
END
