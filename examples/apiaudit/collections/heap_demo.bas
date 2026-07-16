' heap_demo.bas - Comprehensive API audit for Viper.Collections.Heap
' Tests: New, NewMax, Push(priority, value), Pop, Peek, Len, IsEmpty,
'        Clear, IsMax, TryPeekOption, TryPopOption, ToSeq

PRINT "=== Heap API Audit ==="

' --- New (min-heap) ---
PRINT "--- New (min-heap) ---"
DIM h AS Viper.Collections.Heap
h = Viper.Collections.Heap.New()
PRINT h.Count       ' 0
PRINT h.IsEmpty   ' 1
PRINT h.IsMax     ' 0

' --- Push / Len ---
PRINT "--- Push / Len ---"
h.Push(3, "low")
h.Push(1, "urgent")
h.Push(2, "medium")
PRINT h.Count       ' 3
PRINT h.IsEmpty   ' 0

' --- Peek (min-heap: lowest priority first) ---
PRINT "--- Peek ---"
PRINT h.Peek()    ' urgent

' --- Pop (min-heap order) ---
PRINT "--- Pop ---"
PRINT h.Pop()     ' urgent (priority 1)
PRINT h.Count       ' 2
PRINT h.Pop()     ' medium (priority 2)
PRINT h.Count       ' 1
PRINT h.Pop()     ' low (priority 3)
PRINT h.Count       ' 0
PRINT h.IsEmpty   ' 1

' --- TryPeekOption / TryPopOption (safe on empty) ---
PRINT "--- TryPeekOption / TryPopOption ---"
DIM tp AS OBJECT
tp = h.TryPeek()
PRINT tp.IsNone   ' 1
DIM tpop AS OBJECT
tpop = h.TryPop()
PRINT tpop.IsNone ' 1

' --- NewMax (max-heap) ---
PRINT "--- NewMax ---"
DIM mh AS Viper.Collections.Heap
mh = Viper.Collections.Heap.NewMax(1)
PRINT mh.IsMax    ' 1
mh.Push(1, "low")
mh.Push(5, "high")
mh.Push(3, "mid")
PRINT mh.Count      ' 3

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
PRINT seq.Count     ' 5

' --- Clear ---
PRINT "--- Clear ---"
DIM h3 AS Viper.Collections.Heap
h3 = Viper.Collections.Heap.New()
h3.Push(1, "x")
h3.Push(2, "y")
PRINT h3.Count      ' 2
h3.Clear()
PRINT h3.Count      ' 0
PRINT h3.IsEmpty   ' 1

PRINT "=== Heap audit complete ==="
END
