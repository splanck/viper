' stack_demo.bas - Comprehensive API audit for Zanna.Collections.Stack
' Tests: New, Push, Pop, Peek, Len, IsEmpty, Clear

PRINT "=== Stack API Audit ==="

' --- New ---
PRINT "--- New ---"
DIM stk AS Zanna.Collections.Stack
stk = Zanna.Collections.Stack.New()
PRINT stk.Count       ' 0
PRINT stk.IsEmpty   ' 1

' --- Push / Len ---
PRINT "--- Push / Len ---"
stk.Push("first")
stk.Push("second")
stk.Push("third")
PRINT stk.Count       ' 3
PRINT stk.IsEmpty   ' 0

' --- Peek (LIFO - returns top) ---
PRINT "--- Peek ---"
PRINT stk.Peek()    ' third

' --- Pop (LIFO order) ---
PRINT "--- Pop ---"
PRINT stk.Pop()     ' third
PRINT stk.Count       ' 2
PRINT stk.Pop()     ' second
PRINT stk.Count       ' 1
PRINT stk.Peek()    ' first

' --- Push more and verify ---
PRINT "--- Push more ---"
stk.Push(Zanna.Core.Box.I64(42))
stk.Push(Zanna.Core.Box.I64(99))
PRINT stk.Count                          ' 3
PRINT Zanna.Core.Box.ToI64(stk.Pop()) ' 99
PRINT Zanna.Core.Box.ToI64(stk.Pop()) ' 42
PRINT stk.Pop()                        ' first
PRINT stk.Count                          ' 0
PRINT stk.IsEmpty                      ' 1

' --- Clear ---
PRINT "--- Clear ---"
stk.Push("a")
stk.Push("b")
stk.Push("c")
PRINT stk.Count       ' 3
stk.Clear()
PRINT stk.Count       ' 0
PRINT stk.IsEmpty   ' 1

PRINT "=== Stack audit complete ==="
END
