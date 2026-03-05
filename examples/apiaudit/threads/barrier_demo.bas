' =============================================================================
' API Audit: Viper.Threads.Barrier (BASIC)
' =============================================================================
' Tests: New, Arrive, Reset, Parties, Waiting
' =============================================================================

PRINT "=== API Audit: Viper.Threads.Barrier ==="

' --- New ---
PRINT "--- New ---"
DIM b AS OBJECT = Viper.Threads.Barrier.New(1)
PRINT "Created barrier with 1 party"

' --- Parties ---
PRINT "--- Parties ---"
PRINT "Parties: "; b.Parties

' --- Waiting (initial) ---
PRINT "--- Waiting (initial) ---"
PRINT "Waiting: "; b.Waiting

' --- Arrive (single party, immediately releases) ---
PRINT "--- Arrive ---"
DIM idx AS INTEGER = b.Arrive()
PRINT "Arrive returned index: "; idx

' --- Waiting (after arrive, barrier reset) ---
PRINT "--- Waiting (after arrive) ---"
PRINT "Waiting: "; b.Waiting

' --- Arrive (second generation) ---
PRINT "--- Arrive (second generation) ---"
DIM idx2 AS INTEGER = b.Arrive()
PRINT "Arrive returned index: "; idx2

' --- Reset ---
PRINT "--- Reset ---"
b.Reset()
PRINT "Waiting after Reset: "; b.Waiting
PRINT "Parties after Reset: "; b.Parties

' --- New with 3 parties ---
PRINT "--- New with 3 parties ---"
DIM b2 AS OBJECT = Viper.Threads.Barrier.New(3)
PRINT "Parties: "; b2.Parties
PRINT "Waiting: "; b2.Waiting

' --- Multiple single-party cycles ---
PRINT "--- Multiple single-party cycles ---"
DIM b3 AS OBJECT = Viper.Threads.Barrier.New(1)
b3.Arrive()
b3.Arrive()
b3.Arrive()
PRINT "3 cycles completed on single-party barrier"

PRINT "=== Barrier Audit Complete ==="
END
