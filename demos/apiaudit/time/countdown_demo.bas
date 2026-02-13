' =============================================================================
' API Audit: Viper.Time.Countdown - Countdown Timer
' =============================================================================
' Tests: New, Start, Stop, Reset, Elapsed, Remaining, Expired, Interval,
'        SetInterval, IsRunning
' =============================================================================

PRINT "=== API Audit: Viper.Time.Countdown ==="

' --- New ---
PRINT "--- New ---"
DIM cd AS OBJECT
cd = Viper.Time.Countdown.New(5000)
PRINT "New(5000) - 5 second countdown"

' --- Interval ---
PRINT "--- Interval ---"
PRINT "Interval: "; Viper.Time.Countdown.get_Interval(cd)

' --- IsRunning ---
PRINT "--- IsRunning (initial) ---"
PRINT "IsRunning: "; Viper.Time.Countdown.get_IsRunning(cd)

' --- Start ---
PRINT "--- Start ---"
Viper.Time.Countdown.Start(cd)
PRINT "Start done"
PRINT "IsRunning: "; Viper.Time.Countdown.get_IsRunning(cd)

' --- Elapsed ---
PRINT "--- Elapsed ---"
PRINT "Elapsed: "; Viper.Time.Countdown.get_Elapsed(cd)

' --- Remaining ---
PRINT "--- Remaining ---"
PRINT "Remaining: "; Viper.Time.Countdown.get_Remaining(cd)

' --- Expired ---
PRINT "--- Expired ---"
PRINT "Expired: "; Viper.Time.Countdown.get_Expired(cd)

' --- Stop ---
PRINT "--- Stop ---"
Viper.Time.Countdown.Stop(cd)
PRINT "Stop done"
PRINT "IsRunning: "; Viper.Time.Countdown.get_IsRunning(cd)

' --- Reset ---
PRINT "--- Reset ---"
Viper.Time.Countdown.Reset(cd)
PRINT "Reset done"
PRINT "Elapsed after reset: "; Viper.Time.Countdown.get_Elapsed(cd)

' --- SetInterval ---
PRINT "--- SetInterval ---"
Viper.Time.Countdown.set_Interval(cd, 10000)
PRINT "set_Interval(10000)"
PRINT "Interval now: "; Viper.Time.Countdown.get_Interval(cd)

PRINT "=== Countdown Demo Complete ==="
END
