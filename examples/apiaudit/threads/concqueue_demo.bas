' =============================================================================
' API Audit: Viper.Threads.ConcurrentQueue (BASIC)
' =============================================================================
' Tests: New, Enqueue, TryDequeueOption, Dequeue, Peek, Len, IsEmpty, Clear
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
PRINT "Len: "; q.Count

' --- Enqueue ---
PRINT "--- Enqueue ---"
q.Enqueue(Viper.Core.Box.Str("alpha"))
q.Enqueue(Viper.Core.Box.Str("beta"))
q.Enqueue(Viper.Core.Box.Str("gamma"))
PRINT "Enqueued 3 items"

' --- Len (after enqueue) ---
PRINT "--- Len (after enqueue) ---"
PRINT "Len: "; q.Count

' --- IsEmpty (after enqueue) ---
PRINT "--- IsEmpty (after enqueue) ---"
PRINT "IsEmpty: "; q.IsEmpty

' --- Peek ---
PRINT "--- Peek ---"
DIM front AS OBJECT = q.Peek()
PRINT "Peek: "; Viper.Core.Box.ToStr(front)

' --- TryDequeueOption ---
PRINT "--- TryDequeueOption ---"
DIM item1 AS OBJECT = q.TryDequeueOption()
PRINT "TryDequeueOption: "; Viper.Core.Box.ToStr(item1.Unwrap())
PRINT "Len after TryDequeueOption: "; q.Count

' --- Dequeue (blocking, but queue has items) ---
PRINT "--- Dequeue ---"
DIM item2 AS OBJECT = q.Dequeue()
PRINT "Dequeue: "; Viper.Core.Box.ToStr(item2)
PRINT "Len after Dequeue: "; q.Count

' --- TryDequeueOption remaining ---
PRINT "--- TryDequeueOption remaining ---"
DIM item3 AS OBJECT = q.TryDequeueOption()
PRINT "TryDequeueOption: "; Viper.Core.Box.ToStr(item3.Unwrap())

' --- TryDequeueOption on empty ---
PRINT "--- TryDequeueOption on empty ---"
DIM item4 AS OBJECT = q.TryDequeueOption()
PRINT "TryDequeueOption empty: "; item4.IsNone

' --- Clear ---
PRINT "--- Clear ---"
q.Enqueue(Viper.Core.Box.I64(10))
q.Enqueue(Viper.Core.Box.I64(20))
q.Enqueue(Viper.Core.Box.I64(30))
PRINT "Len before Clear: "; q.Count
q.Clear()
PRINT "Len after Clear: "; q.Count
PRINT "IsEmpty after Clear: "; q.IsEmpty

PRINT "=== ConcurrentQueue Audit Complete ==="
END
