' Test operations that might crash

' === Test: Seq.Get negative index ===
PRINT "Test: Seq.Get(-1)"
DIM seq AS Viper.Collections.Seq
seq = Viper.Collections.Seq.New()
seq.Push("a")
seq.Push("b")
Viper.Core.Diagnostics.AssertEqStr(seq.Get(-1), "???", "get -1")
PRINT "Get(-1) completed"
END
