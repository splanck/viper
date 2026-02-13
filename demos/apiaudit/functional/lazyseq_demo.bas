' API Audit: Viper.LazySeq (BASIC)
' NOTE: LazySeq.Range returns raw int64_t pointers, NOT boxed values (BUG A-070)
' So we cannot use Box.ToI64 on Range elements. We test what we can.
PRINT "=== API Audit: Viper.LazySeq ==="

' --- Range creation ---
PRINT "--- Range ---"
DIM s1 AS OBJECT = Viper.LazySeq.Range(1, 10, 1)
PRINT s1.Index
PRINT s1.IsExhausted

' --- Count ---
PRINT "--- Count ---"
DIM s4 AS OBJECT = Viper.LazySeq.Range(1, 5, 1)
PRINT s4.Count()

' --- IsExhausted after Count ---
PRINT "--- IsExhausted after Count ---"
PRINT s4.IsExhausted

' --- Repeat with boxed value ---
PRINT "--- Repeat ---"
DIM box42 AS OBJECT = Viper.Core.Box.I64(42)
DIM rep AS OBJECT = Viper.LazySeq.Repeat(box42, 3)
PRINT rep.Count()

' --- Next with Repeat (returns proper box) ---
PRINT "--- Next (Repeat) ---"
DIM box7 AS OBJECT = Viper.Core.Box.I64(7)
DIM rep2 AS OBJECT = Viper.LazySeq.Repeat(box7, 5)
DIM v1 AS OBJECT = rep2.Next()
PRINT Viper.Core.Box.ToI64(v1)

' --- Peek with Repeat ---
PRINT "--- Peek (Repeat) ---"
DIM v2 AS OBJECT = rep2.Peek()
PRINT Viper.Core.Box.ToI64(v2)
PRINT rep2.Index

' --- Take ---
PRINT "--- Take ---"
DIM s2 AS OBJECT = Viper.LazySeq.Range(1, 100, 1)
DIM taken AS OBJECT = s2.Take(5)
PRINT taken.Count()

' --- Drop ---
PRINT "--- Drop ---"
DIM s3 AS OBJECT = Viper.LazySeq.Range(1, 10, 1)
DIM dropped AS OBJECT = s3.Drop(5)
PRINT dropped.Count()

' --- ToSeqN ---
PRINT "--- ToSeqN ---"
DIM s5 AS OBJECT = Viper.LazySeq.Range(10, 20, 2)
DIM partial AS OBJECT = s5.ToSeqN(3)
PRINT "ToSeqN returned obj"

' --- Concat ---
PRINT "--- Concat ---"
DIM sa AS OBJECT = Viper.LazySeq.Range(1, 3, 1)
DIM sb AS OBJECT = Viper.LazySeq.Range(10, 12, 1)
DIM sc AS OBJECT = sa.Concat(sb)
PRINT sc.Count()

' --- Reset ---
PRINT "--- Reset ---"
DIM s6 AS OBJECT = Viper.LazySeq.Range(1, 3, 1)
PRINT s6.Count()
s6.Reset()
PRINT s6.Index

PRINT "=== LazySeq Audit Complete ==="
END
