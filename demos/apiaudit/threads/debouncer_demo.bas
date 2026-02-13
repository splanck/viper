' =============================================================================
' API Audit: Viper.Threads.Debouncer (BASIC)
' =============================================================================
' Tests: New, Signal, IsReady, Reset, Delay, SignalCount
' =============================================================================

PRINT "=== API Audit: Viper.Threads.Debouncer ==="

' --- New ---
PRINT "--- New ---"
DIM d AS OBJECT = Viper.Threads.Debouncer.New(100)
PRINT "Created debouncer with 100ms delay"

' --- Delay ---
PRINT "--- Delay ---"
PRINT "Delay: "; d.Delay

' --- IsReady (initial) ---
PRINT "--- IsReady (initial) ---"
PRINT "IsReady: "; d.IsReady

' --- SignalCount (initial) ---
PRINT "--- SignalCount (initial) ---"
PRINT "SignalCount: "; d.SignalCount

' --- Signal ---
PRINT "--- Signal ---"
d.Signal()
PRINT "Signal sent"
PRINT "SignalCount after Signal: "; d.SignalCount

' --- IsReady (just signaled) ---
PRINT "--- IsReady (just signaled) ---"
PRINT "IsReady: "; d.IsReady

' --- Multiple signals ---
PRINT "--- Multiple signals ---"
d.Signal()
d.Signal()
PRINT "SignalCount after 3 total: "; d.SignalCount

' --- Reset ---
PRINT "--- Reset ---"
d.Reset()
PRINT "SignalCount after Reset: "; d.SignalCount
PRINT "IsReady after Reset: "; d.IsReady

' --- New with 0 delay ---
PRINT "--- New with 0 delay ---"
DIM d2 AS OBJECT = Viper.Threads.Debouncer.New(0)
PRINT "Delay: "; d2.Delay
d2.Signal()
PRINT "SignalCount: "; d2.SignalCount

PRINT "=== Debouncer Audit Complete ==="
END
