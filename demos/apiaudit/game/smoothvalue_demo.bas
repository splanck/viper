' smoothvalue_demo.bas
PRINT "=== Viper.Game.SmoothValue Demo ==="
DIM sv AS OBJECT
sv = NEW Viper.Game.SmoothValue(0.0, 0.5)
PRINT sv.Value
PRINT sv.Smoothing
PRINT sv.AtTarget
sv.SetImmediate(100.0)
PRINT sv.Value
sv.Impulse(10.0)
PRINT sv.Velocity
PRINT "done"
END
