' lazyseq_demo.bas
PRINT "=== Viper.LazySeq Demo ==="
DIM seq AS OBJECT
seq = Viper.LazySeq.Range(1, 10, 1)
PRINT seq.Index
PRINT seq.IsExhausted
PRINT seq.Count()
seq.Reset()
PRINT seq.Index
PRINT "done"
END
