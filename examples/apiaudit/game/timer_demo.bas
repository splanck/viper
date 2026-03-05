' timer_demo.bas - Comprehensive API audit for Viper.Game.Timer
' Tests: New, Start, StartRepeating, Stop, Reset, Update,
'        IsRunning, IsExpired, IsRepeating, Elapsed, Remaining, Progress, Duration

PRINT "=== Timer API Audit ==="

' --- New ---
PRINT "--- New ---"
DIM t AS OBJECT
t = Viper.Game.Timer.New()
PRINT t.IsRunning    ' 0
PRINT t.IsExpired    ' 0
PRINT t.IsRepeating  ' 0
PRINT t.Elapsed      ' 0
PRINT t.Remaining    ' 0
PRINT t.Progress     ' 0

' --- Start ---
PRINT "--- Start ---"
t.Start(1000)
PRINT t.IsRunning    ' 1
PRINT t.IsExpired    ' 0
PRINT t.IsRepeating  ' 0
PRINT t.Duration     ' 1000

' --- Update (not yet expired) ---
PRINT "--- Update (not expired) ---"
DIM expired AS INTEGER
expired = t.Update()
PRINT expired        ' 0 (first frame, not expired yet)
PRINT t.IsRunning    ' 1

' --- Duration (set) ---
PRINT "--- Duration set ---"
t.Duration = 500
PRINT t.Duration     ' 500

' --- Stop ---
PRINT "--- Stop ---"
t.Stop()
PRINT t.IsRunning    ' 0

' --- Reset ---
PRINT "--- Reset ---"
t.Start(2000)
PRINT t.IsRunning    ' 1
t.Reset()
PRINT t.IsRunning    ' 0 or reset state
PRINT t.Elapsed      ' 0

' --- StartRepeating ---
PRINT "--- StartRepeating ---"
t.StartRepeating(100)
PRINT t.IsRunning    ' 1
PRINT t.IsRepeating  ' 1
PRINT t.Duration     ' 100

' --- Update on repeating timer ---
PRINT "--- Update repeating ---"
DIM rep AS INTEGER
rep = t.Update()
PRINT rep            ' 0 (first frame)
PRINT t.IsRunning    ' 1
PRINT t.IsRepeating  ' 1

' --- Remaining / Progress ---
PRINT "--- Remaining / Progress ---"
PRINT t.Remaining    ' remaining ms
PRINT t.Progress     ' progress percentage

' --- Stop repeating ---
PRINT "--- Stop repeating ---"
t.Stop()
PRINT t.IsRunning    ' 0

PRINT "=== Timer audit complete ==="
END
