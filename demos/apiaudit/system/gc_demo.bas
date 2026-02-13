' =============================================================================
' API Audit: Viper.Memory.GC - Garbage Collector
' =============================================================================
' Tests: Collect, TrackedCount, TotalCollected, PassCount
' =============================================================================

PRINT "=== API Audit: Viper.Memory.GC ==="

' --- TrackedCount (initial) ---
PRINT "--- TrackedCount (initial) ---"
PRINT "TrackedCount: "; Viper.Memory.GC.TrackedCount()

' --- PassCount (initial) ---
PRINT "--- PassCount (initial) ---"
PRINT "PassCount: "; Viper.Memory.GC.PassCount()

' --- TotalCollected (initial) ---
PRINT "--- TotalCollected (initial) ---"
PRINT "TotalCollected: "; Viper.Memory.GC.TotalCollected()

' Create some objects
PRINT "--- Creating objects ---"
DIM a AS OBJECT
DIM b AS OBJECT
DIM c AS OBJECT
a = Viper.Core.Box.I64(1)
b = Viper.Core.Box.I64(2)
c = Viper.Core.Box.Str("test")
PRINT "Created 3 boxed objects"
PRINT "TrackedCount after creation: "; Viper.Memory.GC.TrackedCount()

' --- Collect ---
PRINT "--- Collect ---"
DIM collected AS INTEGER
collected = Viper.Memory.GC.Collect()
PRINT "Collect() returned: "; collected

' --- Post-collection stats ---
PRINT "--- Post-collection stats ---"
PRINT "TrackedCount: "; Viper.Memory.GC.TrackedCount()
PRINT "TotalCollected: "; Viper.Memory.GC.TotalCollected()
PRINT "PassCount: "; Viper.Memory.GC.PassCount()

PRINT "=== GC Demo Complete ==="
END
