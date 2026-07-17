' Test operations that might crash

' === Test: Seq.Get negative index ===
PRINT "Test: Seq.Get(-1)"
DIM seq AS Zanna.Collections.Seq
seq = Zanna.Collections.Seq.New()
seq.Push("a")
seq.Push("b")
Zanna.Core.Diagnostics.AssertEqStr(Zanna.Core.Box.ToStr(seq.Get(-1)), "???", "get -1")
PRINT "Get(-1) completed"
END
