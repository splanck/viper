' =============================================================================
' API Audit: Zanna.Runtime.GC - Garbage Collector
' =============================================================================
' Tests: Collect, TrackedCount, TotalCollected, PassCount
' =============================================================================

PRINT "=== API Audit: Zanna.Runtime.GC ==="

' --- TrackedCount (initial) ---
PRINT "--- TrackedCount (initial) ---"
PRINT "TrackedCount: "; Zanna.Runtime.GC.TrackedCount()

' --- PassCount (initial) ---
PRINT "--- PassCount (initial) ---"
PRINT "PassCount: "; Zanna.Runtime.GC.PassCount()

' --- TotalCollected (initial) ---
PRINT "--- TotalCollected (initial) ---"
PRINT "TotalCollected: "; Zanna.Runtime.GC.TotalCollected()

' Create some objects
PRINT "--- Creating objects ---"
DIM a AS OBJECT
DIM b AS OBJECT
DIM c AS OBJECT
a = Zanna.Core.Box.I64(1)
b = Zanna.Core.Box.I64(2)
c = Zanna.Core.Box.Str("test")
PRINT "Created 3 boxed objects"
PRINT "TrackedCount after creation: "; Zanna.Runtime.GC.TrackedCount()

' --- Collect ---
PRINT "--- Collect ---"
DIM collected AS INTEGER
collected = Zanna.Runtime.GC.Collect()
PRINT "Collect() returned: "; collected

' --- Post-collection stats ---
PRINT "--- Post-collection stats ---"
PRINT "TrackedCount: "; Zanna.Runtime.GC.TrackedCount()
PRINT "TotalCollected: "; Zanna.Runtime.GC.TotalCollected()
PRINT "PassCount: "; Zanna.Runtime.GC.PassCount()

PRINT "=== GC Demo Complete ==="
END
