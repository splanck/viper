' heap_demo.bas - Comprehensive API audit for Viper.Collections.Heap
' Tests: New, NewMax, Push(priority, value), Pop, Peek, Len, IsEmpty,
'        Clear, IsMax, TryPeek, TryPop, ToSeq

PRINT "=== Heap API Audit ==="

' --- New (min-heap) ---
PRINT "--- New (min-heap) ---"
DIM h AS Viper.Collections.Heap
h = Viper.Collections.Heap.New()
PRINT h.Length       ' 0
PRINT h.IsEmpty   ' 1
PRINT h.IsMax     ' 0

' --- Push / Len ---
PRINT "--- Push / Len ---"
h.Push(3, "low")
h.Push(1, "urgent")
h.Push(2, "medium")
PRINT h.Length       ' 3
PRINT h.IsEmpty   ' 0

' --- Peek (min-heap: lowest priority first) ---
PRINT "--- Peek ---"
PRINT h.Peek()    ' urgent

' --- Pop (min-heap order) ---
PRINT "--- Pop ---"
PRINT h.Pop()     ' urgent (priority 1)
PRINT h.Length       ' 2
PRINT h.Pop()     ' medium (priority 2)
PRINT h.Length       ' 1
PRINT h.Pop()     ' low (priority 3)
PRINT h.Length       ' 0
PRINT h.IsEmpty   ' 1

' --- TryPeek / TryPop (safe on empty) ---
PRINT "--- TryPeek / TryPop ---"
DIM tp AS OBJECT
tp = h.TryPeek()
PRINT h.IsEmpty   ' 1
DIM tpop AS OBJECT
tpop = h.TryPop()
PRINT h.IsEmpty   ' 1

' --- NewMax (max-heap) ---
PRINT "--- NewMax ---"
DIM mh AS Viper.Collections.Heap
mh = Viper.Collections.Heap.NewMax(1)
PRINT mh.IsMax    ' 1
mh.Push(1, "low")
mh.Push(5, "high")
mh.Push(3, "mid")
PRINT mh.Length      ' 3

' Max-heap: highest priority first
PRINT mh.Pop()    ' high (priority 5)
PRINT mh.Pop()    ' mid (priority 3)
PRINT mh.Pop()    ' low (priority 1)

' --- ToSeq ---
PRINT "--- ToSeq ---"
DIM h2 AS Viper.Collections.Heap
h2 = Viper.Collections.Heap.New()
h2.Push(5, "e")
h2.Push(1, "a")
h2.Push(3, "c")
h2.Push(2, "b")
h2.Push(4, "d")
DIM seq AS OBJECT
seq = h2.ToSeq()
PRINT seq.Length     ' 5

' --- Clear ---
PRINT "--- Clear ---"
DIM h3 AS Viper.Collections.Heap
h3 = Viper.Collections.Heap.New()
h3.Push(1, "x")
h3.Push(2, "y")
PRINT h3.Length      ' 2
h3.Clear()
PRINT h3.Length      ' 0
PRINT h3.IsEmpty   ' 1

PRINT "=== Heap audit complete ==="
END
