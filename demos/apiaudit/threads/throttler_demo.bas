' =============================================================================
' API Audit: Viper.Threads.Throttler (BASIC)
' =============================================================================
' Tests: New, Try, CanProceed, Reset, Interval, Count, RemainingMs
' =============================================================================

PRINT "=== API Audit: Viper.Threads.Throttler ==="

' --- New ---
PRINT "--- New ---"
DIM t AS OBJECT = Viper.Threads.Throttler.New(1000)
PRINT "Created throttler with 1000ms interval"

' --- Interval ---
PRINT "--- Interval ---"
PRINT "Interval: "; t.Interval

' --- Count (initial) ---
PRINT "--- Count (initial) ---"
PRINT "Count: "; t.Count

' --- CanProceed (initial) ---
PRINT "--- CanProceed (initial) ---"
PRINT "CanProceed: "; t.CanProceed

' --- Try (first) ---
PRINT "--- Try (first) ---"
DIM ok1 AS INTEGER = t.Try()
PRINT "Try: "; ok1
PRINT "Count after first Try: "; t.Count

' --- Try (second, throttled) ---
PRINT "--- Try (second, throttled) ---"
DIM ok2 AS INTEGER = t.Try()
PRINT "Try: "; ok2
PRINT "Count after second Try: "; t.Count

' --- CanProceed (throttled) ---
PRINT "--- CanProceed (throttled) ---"
PRINT "CanProceed: "; t.CanProceed

' --- RemainingMs ---
PRINT "--- RemainingMs ---"
DIM remaining AS INTEGER = t.RemainingMs
PRINT "RemainingMs > 0: "; (remaining > 0)

' --- Reset ---
PRINT "--- Reset ---"
t.Reset()
PRINT "CanProceed after Reset: "; t.CanProceed
DIM ok3 AS INTEGER = t.Try()
PRINT "Try after Reset: "; ok3

' --- New with 0 interval ---
PRINT "--- New with 0 interval ---"
DIM t2 AS OBJECT = Viper.Threads.Throttler.New(0)
PRINT "Interval: "; t2.Interval
DIM a AS INTEGER = t2.Try()
DIM b AS INTEGER = t2.Try()
DIM c AS INTEGER = t2.Try()
PRINT "Three consecutive Try: "; a; ", "; b; ", "; c
PRINT "Count: "; t2.Count

PRINT "=== Throttler Audit Complete ==="
END
