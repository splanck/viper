' =============================================================================
' API Audit: Viper.Threads.Pool (BASIC)
' =============================================================================
' Tests: New, Size, Pending, Active, IsShutdown, Shutdown
' Note: Pool.Submit requires function pointers, so we only test properties
'       and lifecycle management here.
' =============================================================================

PRINT "=== API Audit: Viper.Threads.Pool ==="

' --- New ---
PRINT "--- New ---"
DIM p AS OBJECT = Viper.Threads.Pool.New(2)
PRINT "Created pool with 2 workers"

' --- Size ---
PRINT "--- Size ---"
PRINT "Size: "; p.Size

' --- Pending (initial) ---
PRINT "--- Pending (initial) ---"
PRINT "Pending: "; p.Pending

' --- Active (initial) ---
PRINT "--- Active (initial) ---"
PRINT "Active: "; p.Active

' --- IsShutdown (initial) ---
PRINT "--- IsShutdown (initial) ---"
PRINT "IsShutdown: "; p.IsShutdown

' --- Shutdown ---
PRINT "--- Shutdown ---"
p.Shutdown()
PRINT "IsShutdown after Shutdown: "; p.IsShutdown

' --- New with 1 worker ---
PRINT "--- New with 1 worker ---"
DIM p2 AS OBJECT = Viper.Threads.Pool.New(1)
PRINT "Size: "; p2.Size
PRINT "Pending: "; p2.Pending
PRINT "Active: "; p2.Active
PRINT "IsShutdown: "; p2.IsShutdown

' --- ShutdownNow ---
PRINT "--- ShutdownNow ---"
p2.ShutdownNow()
PRINT "IsShutdown after ShutdownNow: "; p2.IsShutdown

' --- New with 4 workers ---
PRINT "--- New with 4 workers ---"
DIM p3 AS OBJECT = Viper.Threads.Pool.New(4)
PRINT "Size: "; p3.Size
p3.Shutdown()
PRINT "Shutdown complete"

PRINT "=== Pool Audit Complete ==="
END
