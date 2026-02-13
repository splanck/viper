' tween_demo.bas
PRINT "=== Viper.Game.Tween Demo ==="
DIM tw AS OBJECT
tw = NEW Viper.Game.Tween()
PRINT tw.IsRunning
PRINT tw.IsComplete
tw.Start(0.0, 100.0, 60, 0)
PRINT tw.IsRunning
PRINT tw.Duration
PRINT tw.Update()
PRINT tw.Elapsed
tw.Stop()
tw.Reset()
PRINT "done"
END
