' =============================================================================
' API Audit: Zanna.Time.Countdown - Countdown Timer
' =============================================================================
' Tests: New, Start, Stop, Reset, Elapsed, Remaining, Expired, Interval,
'        SetInterval, IsRunning
' =============================================================================

PRINT "=== API Audit: Zanna.Time.Countdown ==="

' --- New ---
PRINT "--- New ---"
DIM cd AS OBJECT
cd = Zanna.Time.Countdown.New(5000)
PRINT "New(5000) - 5 second countdown"

' --- Interval ---
PRINT "--- Interval ---"
PRINT "Interval: "; Zanna.Time.Countdown.get_Interval(cd)

' --- IsRunning ---
PRINT "--- IsRunning (initial) ---"
PRINT "IsRunning: "; Zanna.Time.Countdown.get_IsRunning(cd)

' --- Start ---
PRINT "--- Start ---"
Zanna.Time.Countdown.Start(cd)
PRINT "Start done"
PRINT "IsRunning: "; Zanna.Time.Countdown.get_IsRunning(cd)

' --- Elapsed ---
PRINT "--- Elapsed ---"
PRINT "Elapsed: "; Zanna.Time.Countdown.get_Elapsed(cd)

' --- Remaining ---
PRINT "--- Remaining ---"
PRINT "Remaining: "; Zanna.Time.Countdown.get_Remaining(cd)

' --- Expired ---
PRINT "--- Expired ---"
PRINT "Expired: "; Zanna.Time.Countdown.get_IsExpired(cd)

' --- Stop ---
PRINT "--- Stop ---"
Zanna.Time.Countdown.Stop(cd)
PRINT "Stop done"
PRINT "IsRunning: "; Zanna.Time.Countdown.get_IsRunning(cd)

' --- Reset ---
PRINT "--- Reset ---"
Zanna.Time.Countdown.Reset(cd)
PRINT "Reset done"
PRINT "Elapsed after reset: "; Zanna.Time.Countdown.get_Elapsed(cd)

' --- SetInterval ---
PRINT "--- SetInterval ---"
Zanna.Time.Countdown.set_Interval(cd, 10000)
PRINT "set_Interval(10000)"
PRINT "Interval now: "; Zanna.Time.Countdown.get_Interval(cd)

PRINT "=== Countdown Demo Complete ==="
END
