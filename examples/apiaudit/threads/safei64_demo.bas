' =============================================================================
' API Audit: Viper.Threads.SafeI64 (BASIC)
' =============================================================================
' Tests: New, Get, Set, Add, CompareExchange
' =============================================================================

PRINT "=== API Audit: Viper.Threads.SafeI64 ==="

' --- New ---
PRINT "--- New ---"
DIM cell AS OBJECT = Viper.Threads.SafeI64.New(0)
PRINT "Created SafeI64 with initial value 0"

' --- Get (initial) ---
PRINT "--- Get (initial) ---"
PRINT "Get: "; cell.Get()

' --- Set ---
PRINT "--- Set ---"
cell.Set(42)
PRINT "Get after Set(42): "; cell.Get()

' --- Set negative ---
PRINT "--- Set negative ---"
cell.Set(-100)
PRINT "Get after Set(-100): "; cell.Get()

' --- Set large ---
PRINT "--- Set large ---"
cell.Set(1000000)
PRINT "Get after Set(1000000): "; cell.Get()

' --- Add (positive) ---
PRINT "--- Add (positive) ---"
DIM result1 AS INTEGER = cell.Add(500)
PRINT "Add(500) returned: "; result1
PRINT "Get after Add: "; cell.Get()

' --- Add (negative) ---
PRINT "--- Add (negative) ---"
DIM result2 AS INTEGER = cell.Add(-200)
PRINT "Add(-200) returned: "; result2
PRINT "Get after Add: "; cell.Get()

' --- Add zero ---
PRINT "--- Add zero ---"
DIM result3 AS INTEGER = cell.Add(0)
PRINT "Add(0) returned: "; result3

' --- CompareExchange (match) ---
PRINT "--- CompareExchange (match) ---"
cell.Set(50)
DIM old1 AS INTEGER = cell.CompareExchange(50, 100)
PRINT "CAS(50, 100) old: "; old1
PRINT "Get after CAS: "; cell.Get()

' --- CompareExchange (no match) ---
PRINT "--- CompareExchange (no match) ---"
DIM old2 AS INTEGER = cell.CompareExchange(999, 200)
PRINT "CAS(999, 200) old: "; old2
PRINT "Get after failed CAS: "; cell.Get()

' --- CompareExchange with zero ---
PRINT "--- CompareExchange with zero ---"
cell.Set(0)
DIM old3 AS INTEGER = cell.CompareExchange(0, 77)
PRINT "CAS(0, 77) old: "; old3
PRINT "Get after CAS: "; cell.Get()

' --- New with initial value ---
PRINT "--- New with initial value ---"
DIM cell2 AS OBJECT = Viper.Threads.SafeI64.New(999)
PRINT "New(999) Get: "; cell2.Get()

' --- New with negative initial ---
PRINT "--- New with negative initial ---"
DIM cell3 AS OBJECT = Viper.Threads.SafeI64.New(-42)
PRINT "New(-42) Get: "; cell3.Get()

PRINT "=== SafeI64 Audit Complete ==="
END
