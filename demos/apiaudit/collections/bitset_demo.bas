' bitset_demo.bas - Comprehensive API audit for Viper.Collections.BitSet
' Tests: New, Set, Get, Clear, Toggle, ClearAll, SetAll, Count, Len,
'        IsEmpty, Not, And, Or, Xor, ToString

PRINT "=== BitSet API Audit ==="

' --- New ---
PRINT "--- New ---"
DIM bs AS OBJECT
bs = Viper.Collections.BitSet.New(8)
PRINT bs.Len        ' 8
PRINT bs.Count      ' 0
PRINT bs.IsEmpty    ' 1

' --- Set / Get ---
PRINT "--- Set / Get ---"
bs.Set(0)
bs.Set(2)
bs.Set(4)
bs.Set(7)
PRINT bs.Get(0)     ' 1
PRINT bs.Get(1)     ' 0
PRINT bs.Get(2)     ' 1
PRINT bs.Get(4)     ' 1
PRINT bs.Get(7)     ' 1
PRINT bs.Count      ' 4
PRINT bs.IsEmpty    ' 0

' --- Clear (single bit) ---
PRINT "--- Clear ---"
bs.Clear(2)
PRINT bs.Get(2)     ' 0
PRINT bs.Count      ' 3

' --- Toggle ---
PRINT "--- Toggle ---"
bs.Toggle(1)
PRINT bs.Get(1)     ' 1
bs.Toggle(1)
PRINT bs.Get(1)     ' 0

' --- SetAll ---
PRINT "--- SetAll ---"
bs.SetAll()
PRINT bs.Count      ' 8
PRINT bs.Get(3)     ' 1

' --- ClearAll ---
PRINT "--- ClearAll ---"
bs.ClearAll()
PRINT bs.Count      ' 0
PRINT bs.IsEmpty    ' 1

' --- ToString ---
PRINT "--- ToString ---"
bs.Set(0)
bs.Set(3)
bs.Set(7)
PRINT bs.ToString()

' --- And ---
PRINT "--- And ---"
DIM a AS OBJECT = Viper.Collections.BitSet.New(8)
a.Set(0)
a.Set(1)
a.Set(2)

DIM b AS OBJECT = Viper.Collections.BitSet.New(8)
b.Set(1)
b.Set(2)
b.Set(3)

DIM andResult AS OBJECT = a.And(b)
PRINT andResult.Get(0)  ' 0
PRINT andResult.Get(1)  ' 1
PRINT andResult.Get(2)  ' 1
PRINT andResult.Get(3)  ' 0
PRINT andResult.Count   ' 2

' --- Or ---
PRINT "--- Or ---"
DIM orResult AS OBJECT = a.Or(b)
PRINT orResult.Get(0)   ' 1
PRINT orResult.Get(1)   ' 1
PRINT orResult.Get(2)   ' 1
PRINT orResult.Get(3)   ' 1
PRINT orResult.Count    ' 4

' --- Xor ---
PRINT "--- Xor ---"
DIM xorResult AS OBJECT = a.Xor(b)
PRINT xorResult.Get(0)  ' 1
PRINT xorResult.Get(1)  ' 0
PRINT xorResult.Get(2)  ' 0
PRINT xorResult.Get(3)  ' 1
PRINT xorResult.Count   ' 2

' --- Not ---
PRINT "--- Not ---"
DIM c AS OBJECT = Viper.Collections.BitSet.New(4)
c.Set(0)
c.Set(2)
DIM notResult AS OBJECT = c.Not()
PRINT notResult.Get(0)  ' 0
PRINT notResult.Get(1)  ' 1
PRINT notResult.Get(2)  ' 0
PRINT notResult.Get(3)  ' 1

PRINT "=== BitSet audit complete ==="
END
