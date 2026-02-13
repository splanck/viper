' smoothvalue_demo.bas - Comprehensive API audit for Viper.Game.SmoothValue
' Tests: New(initial,smoothing), Value, ValueI64, Target, Smoothing, AtTarget,
'        Velocity, SetImmediate, Update, Impulse

PRINT "=== SmoothValue API Audit ==="

' --- New (initial=50.0, smoothing=0.1) ---
PRINT "--- New ---"
DIM sv AS OBJECT
sv = Viper.Game.SmoothValue.New(50.0, 0.1)
PRINT sv.Value        ' 50.0
PRINT sv.ValueI64     ' 50
PRINT sv.Target       ' 50.0
PRINT sv.Smoothing    ' 0.1
PRINT sv.AtTarget     ' 1 (value == target)

' --- Target (set) ---
PRINT "--- Target set ---"
sv.Target = 100.0
PRINT sv.Target       ' 100.0
PRINT sv.AtTarget     ' 0 (value != target now)

' --- Update ---
PRINT "--- Update ---"
sv.Update()
PRINT sv.AtTarget     ' 0 (still approaching)
PRINT sv.Velocity     ' non-zero velocity

' --- Multiple updates to approach target ---
PRINT "--- Multiple updates ---"
sv.Update()
sv.Update()
sv.Update()
sv.Update()

' --- Smoothing (set) ---
PRINT "--- Smoothing set ---"
sv.Smoothing = 0.5
PRINT sv.Smoothing    ' 0.5

' --- SetImmediate ---
PRINT "--- SetImmediate ---"
sv.SetImmediate(75.0)
PRINT sv.Value        ' 75.0
PRINT sv.Target       ' 75.0
PRINT sv.AtTarget     ' 1

' --- Impulse ---
PRINT "--- Impulse ---"
sv.Impulse(10.0)
sv.Update()

' --- New with different params ---
PRINT "--- New (zero, fast smoothing) ---"
DIM sv2 AS OBJECT
sv2 = Viper.Game.SmoothValue.New(0.0, 0.9)
PRINT sv2.Value       ' 0.0
PRINT sv2.Smoothing   ' 0.9
sv2.Target = 10.0
sv2.Update()
PRINT sv2.ValueI64    ' should be close to 10

PRINT "=== SmoothValue audit complete ==="
END
