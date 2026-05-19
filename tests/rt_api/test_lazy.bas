' test_lazy.bas — Lazy, LazySeq
DIM lazy AS OBJECT
DIM seq AS OBJECT
DIM out AS OBJECT
DIM count AS INTEGER

lazy = Viper.Functional.Lazy.OfI64(42)
PRINT Viper.Functional.Lazy.GetI64(lazy)

seq = Viper.Functional.LazySeq.Range(1, 5, 1)
out = Viper.Functional.LazySeq.ToSeqN(seq, 3)
count = out.Length
PRINT count

PRINT "done"
END
