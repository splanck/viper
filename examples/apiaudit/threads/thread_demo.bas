' =============================================================================
' API Audit: Viper.Threads.Thread (BASIC)
' =============================================================================
' Tests: Sleep, Yield
' Note: Thread.Start requires function pointers (ptr,ptr), so we test
'       the static utility methods and document the limitation.
' =============================================================================

PRINT "=== API Audit: Viper.Threads.Thread ==="

' --- Sleep ---
PRINT "--- Sleep ---"
PRINT "Sleeping 10ms..."
Viper.Threads.Thread.Sleep(10)
PRINT "Sleep(10) returned"

' --- Sleep zero ---
PRINT "--- Sleep zero ---"
Viper.Threads.Thread.Sleep(0)
PRINT "Sleep(0) returned"

' --- Yield ---
PRINT "--- Yield ---"
Viper.Threads.Thread.Yield()
PRINT "Yield returned"

' --- Multiple yields ---
PRINT "--- Multiple yields ---"
Viper.Threads.Thread.Yield()
Viper.Threads.Thread.Yield()
Viper.Threads.Thread.Yield()
PRINT "3 yields completed"

' --- Sleep longer ---
PRINT "--- Sleep longer ---"
Viper.Threads.Thread.Sleep(50)
PRINT "Sleep(50) returned"

' Note: Thread.Start/Join/TryJoin/JoinFor/Id/IsAlive require ptr args
PRINT "--- Note ---"
PRINT "Start/Join/TryJoin/JoinFor/Id/IsAlive require ptr args (not testable here)"

PRINT "=== Thread Audit Complete ==="
END
