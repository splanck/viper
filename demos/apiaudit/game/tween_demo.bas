' tween_demo.bas - Comprehensive API audit for Viper.Game.Tween
' Tests: New, Start, StartI64, Update, Stop, Reset, Pause, Resume,
'        Value, ValueI64, IsRunning, IsComplete, IsPaused,
'        Progress, Elapsed, Duration, LerpI64, Ease

PRINT "=== Tween API Audit ==="

' --- New ---
PRINT "--- New ---"
DIM tw AS OBJECT
tw = Viper.Game.Tween.New()
PRINT tw.IsRunning    ' 0
PRINT tw.IsComplete   ' 0
PRINT tw.IsPaused     ' 0

' --- Start (f64: from=0.0, to=100.0, duration=10 frames, ease=0/linear) ---
PRINT "--- Start ---"
tw.Start(0.0, 100.0, 10, 0)
PRINT tw.IsRunning    ' 1
PRINT tw.IsComplete   ' 0
PRINT tw.Duration     ' 10
PRINT tw.Value        ' 0.0 (start value)

' --- Update ---
PRINT "--- Update ---"
DIM done AS INTEGER
done = tw.Update()
PRINT done            ' 0 (not complete after 1 frame)
PRINT tw.IsRunning    ' 1
PRINT tw.Elapsed      ' 1
PRINT tw.Progress     ' 10 (10%)

' --- Pause ---
PRINT "--- Pause ---"
tw.Pause()
PRINT tw.IsPaused     ' 1
PRINT tw.IsRunning    ' 1

' --- Resume ---
PRINT "--- Resume ---"
tw.Resume()
PRINT tw.IsPaused     ' 0
PRINT tw.IsRunning    ' 1

' --- Stop ---
PRINT "--- Stop ---"
tw.Stop()
PRINT tw.IsRunning    ' 0

' --- Reset ---
PRINT "--- Reset ---"
tw.Start(0.0, 50.0, 5, 0)
tw.Update()
tw.Reset()
PRINT tw.Elapsed      ' 0
PRINT tw.IsComplete   ' 0

' --- StartI64 (integer tween: from=0, to=200, duration=8, ease=0/linear) ---
PRINT "--- StartI64 ---"
DIM tw2 AS OBJECT
tw2 = Viper.Game.Tween.New()
tw2.StartI64(0, 200, 8, 0)
PRINT tw2.IsRunning   ' 1
PRINT tw2.ValueI64    ' 0
tw2.Update()
PRINT tw2.ValueI64    ' ~25 (200/8)

' --- Start with easing (ease=1 = InQuad) ---
PRINT "--- Start with easing ---"
DIM tw3 AS OBJECT
tw3 = Viper.Game.Tween.New()
tw3.Start(0.0, 100.0, 10, 1)
PRINT tw3.IsRunning   ' 1
tw3.Update()
PRINT tw3.IsRunning   ' 1

' --- LerpI64 (static) ---
PRINT "--- LerpI64 ---"
PRINT Viper.Game.Tween.LerpI64(0, 100, 0.0)   ' 0
PRINT Viper.Game.Tween.LerpI64(0, 100, 0.5)   ' 50
PRINT Viper.Game.Tween.LerpI64(0, 100, 1.0)   ' 100

' --- Ease (static: t, ease_type) ---
PRINT "--- Ease ---"
PRINT Viper.Game.Tween.Ease(0.0, 0)   ' 0.0 (linear at 0)
PRINT Viper.Game.Tween.Ease(0.5, 0)   ' 0.5 (linear at 0.5)
PRINT Viper.Game.Tween.Ease(1.0, 0)   ' 1.0 (linear at 1)

PRINT "=== Tween audit complete ==="
END
