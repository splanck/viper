' test_lazy.bas — Lazy, LazySeq
DIM lazy AS OBJECT
DIM seq AS OBJECT
DIM out AS OBJECT
DIM count AS INTEGER

lazy = Viper.Lazy.OfI64(42)
PRINT Viper.Lazy.GetI64(lazy)

seq = Viper.LazySeq.Range(1, 5, 1)
out = Viper.LazySeq.ToSeqN(seq, 3)
count = out.Length
PRINT count

PRINT "done"
END
