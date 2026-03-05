' =============================================================================
' API Audit: Viper.Threads.CancelToken (BASIC)
' =============================================================================
' Tests: New, IsCancelled, Cancel, Reset, Check, Linked
' =============================================================================

PRINT "=== API Audit: Viper.Threads.CancelToken ==="

' --- New ---
PRINT "--- New ---"
DIM token AS OBJECT = Viper.Threads.CancelToken.New()
PRINT "Created token"

' --- IsCancelled (initial) ---
PRINT "--- IsCancelled (initial) ---"
PRINT "IsCancelled: "; token.IsCancelled

' --- Cancel ---
PRINT "--- Cancel ---"
token.Cancel()
PRINT "IsCancelled after Cancel: "; token.IsCancelled

' --- Reset ---
PRINT "--- Reset ---"
token.Reset()
PRINT "IsCancelled after Reset: "; token.IsCancelled

' --- Check (not cancelled) ---
PRINT "--- Check (not cancelled) ---"
PRINT "Check: "; token.Check()

' --- Cancel and Check ---
PRINT "--- Cancel and Check ---"
token.Cancel()
PRINT "Check after Cancel: "; token.Check()

' --- Reset for linked test ---
token.Reset()

' --- Linked ---
' Note: RT_METHOD "Linked" signature mismatch with RT_FUNC - use function-style call
PRINT "--- Linked ---"
DIM child AS OBJECT = Viper.Threads.CancelToken.Linked(token)
PRINT "Child IsCancelled: "; child.IsCancelled
PRINT "Child Check: "; child.Check()

' --- Linked: cancel parent ---
PRINT "--- Linked: cancel parent ---"
token.Cancel()
PRINT "Parent IsCancelled: "; token.IsCancelled
PRINT "Child Check (parent cancelled): "; child.Check()

' --- Linked: independent cancel ---
PRINT "--- Linked: independent cancel ---"
token.Reset()
DIM child2 AS OBJECT = Viper.Threads.CancelToken.Linked(token)
child2.Cancel()
PRINT "Child2 IsCancelled: "; child2.IsCancelled
PRINT "Parent IsCancelled: "; token.IsCancelled

PRINT "=== CancelToken Audit Complete ==="
END
