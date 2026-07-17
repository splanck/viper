' =============================================================================
' API Audit: Zanna.Threads.Channel (BASIC)
' =============================================================================
' Tests: New, Send, TrySend, Recv, TryRecvOption, Close, Len, Capacity, IsClosed,
'        IsEmpty, IsFull
' Note: Channel has no RT_CLASS so we use function-style calls.
' =============================================================================

PRINT "=== API Audit: Zanna.Threads.Channel ==="

' --- New ---
PRINT "--- New ---"
DIM ch AS OBJECT = Zanna.Threads.Channel.New(3)
PRINT "Created channel with capacity 3"

' --- Capacity ---
PRINT "--- Capacity ---"
PRINT "Capacity: "; Zanna.Threads.Channel.get_Capacity(ch)

' --- Len (initial) ---
PRINT "--- Len (initial) ---"
PRINT "Len: "; Zanna.Threads.Channel.get_Count(ch)

' --- IsEmpty (initial) ---
PRINT "--- IsEmpty (initial) ---"
PRINT "IsEmpty: "; Zanna.Threads.Channel.get_IsEmpty(ch)

' --- IsFull (initial) ---
PRINT "--- IsFull (initial) ---"
PRINT "IsFull: "; Zanna.Threads.Channel.get_IsFull(ch)

' --- IsClosed (initial) ---
PRINT "--- IsClosed (initial) ---"
PRINT "IsClosed: "; Zanna.Threads.Channel.get_IsClosed(ch)

' --- Send ---
PRINT "--- Send ---"
Zanna.Threads.Channel.Send(ch, Zanna.Core.Box.Str("hello"))
PRINT "Len after Send: "; Zanna.Threads.Channel.get_Count(ch)

' --- TrySend ---
PRINT "--- TrySend ---"
DIM ok1 AS INTEGER = Zanna.Threads.Channel.TrySend(ch, Zanna.Core.Box.Str("world"))
PRINT "TrySend: "; ok1
PRINT "Len after TrySend: "; Zanna.Threads.Channel.get_Count(ch)

' --- Fill channel ---
PRINT "--- Fill channel ---"
Zanna.Threads.Channel.Send(ch, Zanna.Core.Box.Str("third"))
PRINT "Len: "; Zanna.Threads.Channel.get_Count(ch)
PRINT "IsFull: "; Zanna.Threads.Channel.get_IsFull(ch)

' --- TrySend (full) ---
PRINT "--- TrySend (full) ---"
DIM ok2 AS INTEGER = Zanna.Threads.Channel.TrySend(ch, Zanna.Core.Box.Str("overflow"))
PRINT "TrySend (full): "; ok2

' --- IsEmpty (not empty) ---
PRINT "--- IsEmpty (not empty) ---"
PRINT "IsEmpty: "; Zanna.Threads.Channel.get_IsEmpty(ch)

' --- Recv ---
PRINT "--- Recv ---"
DIM item1 AS OBJECT = Zanna.Threads.Channel.Recv(ch)
PRINT "Recv: "; Zanna.Core.Box.ToStr(item1)
PRINT "Len after Recv: "; Zanna.Threads.Channel.get_Count(ch)

' --- TryRecvOption ---
PRINT "--- TryRecvOption ---"
DIM item2 AS OBJECT = Zanna.Threads.Channel.TryRecv(ch)
PRINT "TryRecvOption: "; Zanna.Core.Box.ToStr(item2.Unwrap())

' --- Drain remaining ---
PRINT "--- Drain remaining ---"
DIM item3 AS OBJECT = Zanna.Threads.Channel.Recv(ch)
PRINT "Recv: "; Zanna.Core.Box.ToStr(item3)
PRINT "Len: "; Zanna.Threads.Channel.get_Count(ch)
PRINT "IsEmpty: "; Zanna.Threads.Channel.get_IsEmpty(ch)

' --- Close ---
PRINT "--- Close ---"
Zanna.Threads.Channel.Close(ch)
PRINT "IsClosed: "; Zanna.Threads.Channel.get_IsClosed(ch)

' --- TrySend (closed) ---
PRINT "--- TrySend (closed) ---"
DIM ok3 AS INTEGER = Zanna.Threads.Channel.TrySend(ch, Zanna.Core.Box.Str("after close"))
PRINT "TrySend (closed): "; ok3

' --- Recv from second channel ---
PRINT "--- Recv I64 test ---"
DIM ch2 AS OBJECT = Zanna.Threads.Channel.New(5)
Zanna.Threads.Channel.Send(ch2, Zanna.Core.Box.I64(42))
DIM recv1 AS OBJECT = Zanna.Threads.Channel.Recv(ch2)
PRINT "Recv I64: "; Zanna.Core.Box.ToI64(recv1)

' --- Channel capacity 1 ---
PRINT "--- Channel capacity 1 ---"
DIM ch3 AS OBJECT = Zanna.Threads.Channel.New(1)
PRINT "Capacity: "; Zanna.Threads.Channel.get_Capacity(ch3)
Zanna.Threads.Channel.Send(ch3, Zanna.Core.Box.Str("single"))
PRINT "IsFull: "; Zanna.Threads.Channel.get_IsFull(ch3)
DIM s AS OBJECT = Zanna.Threads.Channel.Recv(ch3)
PRINT "Recv: "; Zanna.Core.Box.ToStr(s)
PRINT "IsEmpty: "; Zanna.Threads.Channel.get_IsEmpty(ch3)

PRINT "=== Channel Audit Complete ==="
END
