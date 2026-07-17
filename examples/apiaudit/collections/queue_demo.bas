' queue_demo.bas - Comprehensive API audit for Zanna.Collections.Queue
' Tests: New, Push, Pop, Peek, Len, IsEmpty, Clear

PRINT "=== Queue API Audit ==="

' --- New ---
PRINT "--- New ---"
DIM q AS Zanna.Collections.Queue
q = Zanna.Collections.Queue.New()
PRINT q.Count       ' 0
PRINT q.IsEmpty   ' 1

' --- Push / Len ---
PRINT "--- Push / Len ---"
q.Push("first")
q.Push("second")
q.Push("third")
PRINT q.Count       ' 3
PRINT q.IsEmpty   ' 0

' --- Peek (FIFO - returns front) ---
PRINT "--- Peek ---"
PRINT q.Peek()    ' first

' --- Pop (FIFO order) ---
PRINT "--- Pop ---"
PRINT q.Pop()     ' first
PRINT q.Count       ' 2
PRINT q.Pop()     ' second
PRINT q.Count       ' 1
PRINT q.Peek()    ' third

' --- Push more and verify FIFO ---
PRINT "--- Push more ---"
q.Push(Zanna.Core.Box.I64(42))
q.Push(Zanna.Core.Box.I64(99))
PRINT q.Count                          ' 3
PRINT q.Pop()                        ' third
PRINT Zanna.Core.Box.ToI64(q.Pop()) ' 42
PRINT Zanna.Core.Box.ToI64(q.Pop()) ' 99
PRINT q.Count                          ' 0
PRINT q.IsEmpty                      ' 1

' --- Clear ---
PRINT "--- Clear ---"
q.Push("a")
q.Push("b")
q.Push("c")
PRINT q.Count       ' 3
q.Clear()
PRINT q.Count       ' 0
PRINT q.IsEmpty   ' 1

PRINT "=== Queue audit complete ==="
END
