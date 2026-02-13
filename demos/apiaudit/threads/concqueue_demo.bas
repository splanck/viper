' =============================================================================
' API Audit: Viper.Threads.ConcurrentQueue (BASIC)
' =============================================================================
' Tests: New, Enqueue, TryDequeue, Dequeue, Peek, Len, IsEmpty, Clear
' =============================================================================

PRINT "=== API Audit: Viper.Threads.ConcurrentQueue ==="

' --- New ---
PRINT "--- New ---"
DIM q AS OBJECT = Viper.Threads.ConcurrentQueue.New()
PRINT "Created queue"

' --- IsEmpty (initial) ---
PRINT "--- IsEmpty (initial) ---"
PRINT "IsEmpty: "; q.IsEmpty

' --- Len (initial) ---
PRINT "--- Len (initial) ---"
PRINT "Len: "; q.Len

' --- Enqueue ---
PRINT "--- Enqueue ---"
q.Enqueue(Viper.Core.Box.Str("alpha"))
q.Enqueue(Viper.Core.Box.Str("beta"))
q.Enqueue(Viper.Core.Box.Str("gamma"))
PRINT "Enqueued 3 items"

' --- Len (after enqueue) ---
PRINT "--- Len (after enqueue) ---"
PRINT "Len: "; q.Len

' --- IsEmpty (after enqueue) ---
PRINT "--- IsEmpty (after enqueue) ---"
PRINT "IsEmpty: "; q.IsEmpty

' --- Peek ---
PRINT "--- Peek ---"
DIM front AS OBJECT = q.Peek()
PRINT "Peek: "; Viper.Core.Box.ToStr(front)

' --- TryDequeue ---
PRINT "--- TryDequeue ---"
DIM item1 AS OBJECT = q.TryDequeue()
PRINT "TryDequeue: "; Viper.Core.Box.ToStr(item1)
PRINT "Len after TryDequeue: "; q.Len

' --- Dequeue (blocking, but queue has items) ---
PRINT "--- Dequeue ---"
DIM item2 AS OBJECT = q.Dequeue()
PRINT "Dequeue: "; Viper.Core.Box.ToStr(item2)
PRINT "Len after Dequeue: "; q.Len

' --- TryDequeue remaining ---
PRINT "--- TryDequeue remaining ---"
DIM item3 AS OBJECT = q.TryDequeue()
PRINT "TryDequeue: "; Viper.Core.Box.ToStr(item3)

' --- TryDequeue on empty ---
PRINT "--- TryDequeue on empty ---"
DIM item4 AS OBJECT = q.TryDequeue()
PRINT "TryDequeue on empty returned (expect empty for null)"

' --- Clear ---
PRINT "--- Clear ---"
q.Enqueue(Viper.Core.Box.I64(10))
q.Enqueue(Viper.Core.Box.I64(20))
q.Enqueue(Viper.Core.Box.I64(30))
PRINT "Len before Clear: "; q.Len
q.Clear()
PRINT "Len after Clear: "; q.Len
PRINT "IsEmpty after Clear: "; q.IsEmpty

PRINT "=== ConcurrentQueue Audit Complete ==="
END
