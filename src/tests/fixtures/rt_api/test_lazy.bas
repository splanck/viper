' test_lazy.bas — Lazy, LazySeq
DIM lazy AS OBJECT
DIM seq AS OBJECT
DIM out AS OBJECT
DIM count AS INTEGER

lazy = Zanna.Functional.Lazy.OfI64(42)
PRINT Zanna.Functional.Lazy.GetI64(lazy)

seq = Zanna.Functional.LazySeq.Range(1, 5, 1)
out = Zanna.Functional.LazySeq.ToSeqLimited(seq, 3)
count = out.Count
PRINT count

PRINT "done"
END
