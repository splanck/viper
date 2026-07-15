' =============================================================================
' API Audit: Viper.Runtime.GC - Garbage Collector
' =============================================================================
' Tests: Collect, TrackedCount, TotalCollected, PassCount
' =============================================================================

PRINT "=== API Audit: Viper.Runtime.GC ==="

' --- TrackedCount (initial) ---
PRINT "--- TrackedCount (initial) ---"
PRINT "TrackedCount: "; Viper.Runtime.GC.TrackedCount()

' --- PassCount (initial) ---
PRINT "--- PassCount (initial) ---"
PRINT "PassCount: "; Viper.Runtime.GC.PassCount()

' --- TotalCollected (initial) ---
PRINT "--- TotalCollected (initial) ---"
PRINT "TotalCollected: "; Viper.Runtime.GC.TotalCollected()

' Create some objects
PRINT "--- Creating objects ---"
DIM a AS OBJECT
DIM b AS OBJECT
DIM c AS OBJECT
a = Viper.Core.Box.I64(1)
b = Viper.Core.Box.I64(2)
c = Viper.Core.Box.Str("test")
PRINT "Created 3 boxed objects"
PRINT "TrackedCount after creation: "; Viper.Runtime.GC.TrackedCount()

' --- Collect ---
PRINT "--- Collect ---"
DIM collected AS INTEGER
collected = Viper.Runtime.GC.Collect()
PRINT "Collect() returned: "; collected

' --- Post-collection stats ---
PRINT "--- Post-collection stats ---"
PRINT "TrackedCount: "; Viper.Runtime.GC.TrackedCount()
PRINT "TotalCollected: "; Viper.Runtime.GC.TotalCollected()
PRINT "PassCount: "; Viper.Runtime.GC.PassCount()

PRINT "=== GC Demo Complete ==="
END
