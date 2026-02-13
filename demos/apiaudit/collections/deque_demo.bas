' deque_demo.bas - Comprehensive API audit for Viper.Collections.Deque
' Tests: New, PushFront, PushBack, PopFront, PopBack, PeekFront, PeekBack,
'        Get, Set, Len, Cap, IsEmpty, Clear, Has, Reverse, Clone

PRINT "=== Deque API Audit ==="

' --- New ---
PRINT "--- New ---"
DIM dq AS OBJECT
dq = Viper.Collections.Deque.New()
PRINT dq.Len       ' 0
PRINT dq.IsEmpty   ' 1

' --- PushBack / Len ---
PRINT "--- PushBack / Len ---"
dq.PushBack("a")
dq.PushBack("b")
dq.PushBack("c")
PRINT dq.Len       ' 3
PRINT dq.IsEmpty   ' 0

' --- PushFront ---
PRINT "--- PushFront ---"
dq.PushFront("z")
PRINT dq.Len       ' 4
' Order: z, a, b, c

' --- PeekFront / PeekBack ---
PRINT "--- PeekFront / PeekBack ---"
PRINT dq.PeekFront()  ' z
PRINT dq.PeekBack()   ' c

' --- Get ---
PRINT "--- Get ---"
PRINT dq.Get(0)  ' z
PRINT dq.Get(1)  ' a
PRINT dq.Get(2)  ' b
PRINT dq.Get(3)  ' c

' --- Set ---
PRINT "--- Set ---"
dq.Set(1, Viper.Core.Box.Str("A"))
PRINT dq.Get(1)  ' A

' --- PopFront ---
PRINT "--- PopFront ---"
PRINT dq.PopFront()   ' z
PRINT dq.Len          ' 3
PRINT dq.PeekFront()  ' A

' --- PopBack ---
PRINT "--- PopBack ---"
PRINT dq.PopBack()    ' c
PRINT dq.Len          ' 2
PRINT dq.PeekBack()   ' b

' --- Has ---
PRINT "--- Has ---"
PRINT dq.Has(Viper.Core.Box.Str("A"))  ' 1
PRINT dq.Has(Viper.Core.Box.Str("z"))  ' 0

' --- Cap ---
PRINT "--- Cap ---"
PRINT dq.Cap  ' capacity (>= Len)

' --- Reverse ---
PRINT "--- Reverse ---"
dq.PushBack("x")
dq.PushBack("y")
' Order: A, b, x, y
dq.Reverse()
PRINT dq.PeekFront()  ' y
PRINT dq.PeekBack()   ' A

' --- Clone ---
PRINT "--- Clone ---"
DIM dq2 AS OBJECT
dq2 = dq.Clone()
PRINT dq2.Len          ' same as dq
PRINT dq2.PeekFront()  ' y

' --- Clear ---
PRINT "--- Clear ---"
dq.Clear()
PRINT dq.Len       ' 0
PRINT dq.IsEmpty   ' 1
PRINT dq2.Len      ' still 4

PRINT "=== Deque audit complete ==="
END
