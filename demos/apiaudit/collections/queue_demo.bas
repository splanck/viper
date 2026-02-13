' queue_demo.bas - Comprehensive API audit for Viper.Collections.Queue
' Tests: New, Push, Pop, Peek, Len, IsEmpty, Clear

PRINT "=== Queue API Audit ==="

' --- New ---
PRINT "--- New ---"
DIM q AS Viper.Collections.Queue
q = Viper.Collections.Queue.New()
PRINT q.Len       ' 0
PRINT q.IsEmpty   ' 1

' --- Push / Len ---
PRINT "--- Push / Len ---"
q.Push("first")
q.Push("second")
q.Push("third")
PRINT q.Len       ' 3
PRINT q.IsEmpty   ' 0

' --- Peek (FIFO - returns front) ---
PRINT "--- Peek ---"
PRINT q.Peek()    ' first

' --- Pop (FIFO order) ---
PRINT "--- Pop ---"
PRINT q.Pop()     ' first
PRINT q.Len       ' 2
PRINT q.Pop()     ' second
PRINT q.Len       ' 1
PRINT q.Peek()    ' third

' --- Push more and verify FIFO ---
PRINT "--- Push more ---"
q.Push(Viper.Core.Box.I64(42))
q.Push(Viper.Core.Box.I64(99))
PRINT q.Len                          ' 3
PRINT q.Pop()                        ' third
PRINT Viper.Core.Box.ToI64(q.Pop()) ' 42
PRINT Viper.Core.Box.ToI64(q.Pop()) ' 99
PRINT q.Len                          ' 0
PRINT q.IsEmpty                      ' 1

' --- Clear ---
PRINT "--- Clear ---"
q.Push("a")
q.Push("b")
q.Push("c")
PRINT q.Len       ' 3
q.Clear()
PRINT q.Len       ' 0
PRINT q.IsEmpty   ' 1

PRINT "=== Queue audit complete ==="
END
