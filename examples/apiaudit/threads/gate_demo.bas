' =============================================================================
' API Audit: Viper.Threads.Gate (BASIC)
' =============================================================================
' Tests: New, Enter, TryEnter, Leave, LeaveMany, Permits
' =============================================================================

PRINT "=== API Audit: Viper.Threads.Gate ==="

' --- New ---
PRINT "--- New ---"
DIM g AS OBJECT = Viper.Threads.Gate.New(3)
PRINT "Created gate with 3 permits"

' --- Permits (initial) ---
PRINT "--- Permits (initial) ---"
PRINT "Permits: "; g.Permits

' --- Enter ---
PRINT "--- Enter ---"
g.Enter()
PRINT "Permits after Enter: "; g.Permits

' --- TryEnter ---
PRINT "--- TryEnter ---"
DIM ok1 AS INTEGER = g.TryEnter()
PRINT "TryEnter: "; ok1
PRINT "Permits after TryEnter: "; g.Permits

' --- Enter (last permit) ---
PRINT "--- Enter (last permit) ---"
g.Enter()
PRINT "Permits: "; g.Permits

' --- TryEnter (no permits) ---
PRINT "--- TryEnter (no permits) ---"
DIM ok2 AS INTEGER = g.TryEnter()
PRINT "TryEnter (no permits): "; ok2

' --- Leave ---
PRINT "--- Leave ---"
g.Leave()
PRINT "Permits after Leave: "; g.Permits

' --- Leave(n) ---
PRINT "--- Leave(n) ---"
g.Leave(2)
PRINT "Permits after Leave(2): "; g.Permits

' --- TryEnter after leave ---
PRINT "--- TryEnter after leave ---"
DIM ok3 AS INTEGER = g.TryEnter()
PRINT "TryEnter: "; ok3
g.Leave()

' --- New with 1 permit (mutex-like) ---
PRINT "--- New with 1 permit (mutex-like) ---"
DIM mutex AS OBJECT = Viper.Threads.Gate.New(1)
PRINT "Permits: "; mutex.Permits
mutex.Enter()
PRINT "Permits after Enter: "; mutex.Permits
DIM ok4 AS INTEGER = mutex.TryEnter()
PRINT "TryEnter (locked): "; ok4
mutex.Leave()
PRINT "Permits after Leave: "; mutex.Permits

' --- New with 0 permits ---
PRINT "--- New with 0 permits ---"
DIM empty AS OBJECT = Viper.Threads.Gate.New(0)
PRINT "Permits: "; empty.Permits
DIM ok5 AS INTEGER = empty.TryEnter()
PRINT "TryEnter (0 permits): "; ok5
empty.Leave()
PRINT "Permits after Leave: "; empty.Permits

PRINT "=== Gate Audit Complete ==="
END
