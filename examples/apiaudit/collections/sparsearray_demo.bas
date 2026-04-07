' sparsearray_demo.bas - Comprehensive API audit for Viper.Collections.SparseArray
' Tests: New, Set, Get, Has, Remove, Len, Indices, Values, Clear

PRINT "=== SparseArray API Audit ==="

' --- New ---
PRINT "--- New ---"
DIM sa AS OBJECT
sa = Viper.Collections.SparseArray.New()
PRINT sa.Length       ' 0

' --- Set / Len ---
PRINT "--- Set / Len ---"
sa.Set(0, "zero")
sa.Set(100, "hundred")
sa.Set(1000, "thousand")
sa.Set(-5, "negative")
PRINT sa.Length       ' 4

' --- Get ---
PRINT "--- Get ---"
PRINT sa.Get(0)       ' zero
PRINT sa.Get(100)     ' hundred
PRINT sa.Get(1000)    ' thousand
PRINT sa.Get(-5)      ' negative

' --- Has ---
PRINT "--- Has ---"
PRINT sa.Has(0)       ' 1
PRINT sa.Has(100)     ' 1
PRINT sa.Has(50)      ' 0
PRINT sa.Has(-5)      ' 1

' --- Set (update existing) ---
PRINT "--- Set (update) ---"
sa.Set(100, "HUNDRED")
PRINT sa.Get(100)     ' HUNDRED
PRINT sa.Length          ' 4

' --- Remove ---
PRINT "--- Remove ---"
PRINT sa.Remove(1000)    ' 1
PRINT sa.Has(1000)       ' 0
PRINT sa.Length             ' 3
PRINT sa.Remove(1000)    ' 0

' --- Indices ---
PRINT "--- Indices ---"
DIM indices AS OBJECT
indices = sa.Indices()
PRINT indices.Length        ' 3

' --- Values ---
PRINT "--- Values ---"
DIM vals AS OBJECT
vals = sa.Values()
PRINT vals.Length           ' 3

' --- Clear ---
PRINT "--- Clear ---"
sa.Clear()
PRINT sa.Length             ' 0

PRINT "=== SparseArray audit complete ==="
END
