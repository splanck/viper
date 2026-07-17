' API Audit: Zanna.Functional.Lazy (BASIC)
PRINT "=== API Audit: Zanna.Functional.Lazy ==="

' --- OfI64 / GetI64 ---
PRINT "--- OfI64 / GetI64 ---"
DIM l1 AS OBJECT = Zanna.Functional.Lazy.OfI64(42)
PRINT l1.GetI64()

' --- IsEvaluated ---
PRINT "--- IsEvaluated ---"
DIM l2 AS OBJECT = Zanna.Functional.Lazy.OfI64(100)
PRINT l2.IsEvaluated
DIM dummy AS INTEGER = l2.GetI64()
PRINT l2.IsEvaluated

' --- OfStr / GetStr ---
PRINT "--- OfStr / GetStr ---"
DIM l3 AS OBJECT = Zanna.Functional.Lazy.OfStr("hello lazy")
PRINT l3.GetStr()

' --- Force ---
PRINT "--- Force ---"
DIM l4 AS OBJECT = Zanna.Functional.Lazy.OfI64(77)
PRINT l4.IsEvaluated
l4.Force()
PRINT l4.IsEvaluated

' --- Of (generic) ---
PRINT "--- Of (generic) ---"
DIM boxed AS OBJECT = Zanna.Core.Box.I64(999)
DIM l5 AS OBJECT = Zanna.Functional.Lazy.Of(boxed)
DIM got AS OBJECT = l5.Get()
PRINT Zanna.Core.Box.ToI64(got)

PRINT "=== Lazy Audit Complete ==="
END
