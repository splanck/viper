' =============================================================================
' API Audit: Viper.Threads.Channel (BASIC)
' =============================================================================
' Tests: New, Send, TrySend, Recv, Close, Len, Cap, IsClosed,
'        IsEmpty, IsFull
' Note: Channel has no RT_CLASS so we use function-style calls.
' =============================================================================

PRINT "=== API Audit: Viper.Threads.Channel ==="

' --- New ---
PRINT "--- New ---"
DIM ch AS OBJECT = Viper.Threads.Channel.New(3)
PRINT "Created channel with capacity 3"

' --- Cap ---
PRINT "--- Cap ---"
PRINT "Cap: "; Viper.Threads.Channel.get_Cap(ch)

' --- Len (initial) ---
PRINT "--- Len (initial) ---"
PRINT "Len: "; Viper.Threads.Channel.get_Len(ch)

' --- IsEmpty (initial) ---
PRINT "--- IsEmpty (initial) ---"
PRINT "IsEmpty: "; Viper.Threads.Channel.get_IsEmpty(ch)

' --- IsFull (initial) ---
PRINT "--- IsFull (initial) ---"
PRINT "IsFull: "; Viper.Threads.Channel.get_IsFull(ch)

' --- IsClosed (initial) ---
PRINT "--- IsClosed (initial) ---"
PRINT "IsClosed: "; Viper.Threads.Channel.get_IsClosed(ch)

' --- Send ---
PRINT "--- Send ---"
Viper.Threads.Channel.Send(ch, Viper.Core.Box.Str("hello"))
PRINT "Len after Send: "; Viper.Threads.Channel.get_Len(ch)

' --- TrySend ---
PRINT "--- TrySend ---"
DIM ok1 AS INTEGER = Viper.Threads.Channel.TrySend(ch, Viper.Core.Box.Str("world"))
PRINT "TrySend: "; ok1
PRINT "Len after TrySend: "; Viper.Threads.Channel.get_Len(ch)

' --- Fill channel ---
PRINT "--- Fill channel ---"
Viper.Threads.Channel.Send(ch, Viper.Core.Box.Str("third"))
PRINT "Len: "; Viper.Threads.Channel.get_Len(ch)
PRINT "IsFull: "; Viper.Threads.Channel.get_IsFull(ch)

' --- TrySend (full) ---
PRINT "--- TrySend (full) ---"
DIM ok2 AS INTEGER = Viper.Threads.Channel.TrySend(ch, Viper.Core.Box.Str("overflow"))
PRINT "TrySend (full): "; ok2

' --- IsEmpty (not empty) ---
PRINT "--- IsEmpty (not empty) ---"
PRINT "IsEmpty: "; Viper.Threads.Channel.get_IsEmpty(ch)

' --- Recv ---
PRINT "--- Recv ---"
DIM item1 AS OBJECT = Viper.Threads.Channel.Recv(ch)
PRINT "Recv: "; Viper.Core.Box.ToStr(item1)
PRINT "Len after Recv: "; Viper.Threads.Channel.get_Len(ch)

' --- Recv more ---
DIM item2 AS OBJECT = Viper.Threads.Channel.Recv(ch)
PRINT "Recv: "; Viper.Core.Box.ToStr(item2)

' --- Drain remaining ---
PRINT "--- Drain remaining ---"
DIM item3 AS OBJECT = Viper.Threads.Channel.Recv(ch)
PRINT "Recv: "; Viper.Core.Box.ToStr(item3)
PRINT "Len: "; Viper.Threads.Channel.get_Len(ch)
PRINT "IsEmpty: "; Viper.Threads.Channel.get_IsEmpty(ch)

' --- Close ---
PRINT "--- Close ---"
Viper.Threads.Channel.Close(ch)
PRINT "IsClosed: "; Viper.Threads.Channel.get_IsClosed(ch)

' --- TrySend (closed) ---
PRINT "--- TrySend (closed) ---"
DIM ok3 AS INTEGER = Viper.Threads.Channel.TrySend(ch, Viper.Core.Box.Str("after close"))
PRINT "TrySend (closed): "; ok3

' --- Recv from second channel ---
PRINT "--- Recv I64 test ---"
DIM ch2 AS OBJECT = Viper.Threads.Channel.New(5)
Viper.Threads.Channel.Send(ch2, Viper.Core.Box.I64(42))
DIM recv1 AS OBJECT = Viper.Threads.Channel.Recv(ch2)
PRINT "Recv I64: "; Viper.Core.Box.ToI64(recv1)

' --- Channel capacity 1 ---
PRINT "--- Channel capacity 1 ---"
DIM ch3 AS OBJECT = Viper.Threads.Channel.New(1)
PRINT "Cap: "; Viper.Threads.Channel.get_Cap(ch3)
Viper.Threads.Channel.Send(ch3, Viper.Core.Box.Str("single"))
PRINT "IsFull: "; Viper.Threads.Channel.get_IsFull(ch3)
DIM s AS OBJECT = Viper.Threads.Channel.Recv(ch3)
PRINT "Recv: "; Viper.Core.Box.ToStr(s)
PRINT "IsEmpty: "; Viper.Threads.Channel.get_IsEmpty(ch3)

PRINT "=== Channel Audit Complete ==="
END
