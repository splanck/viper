' bag_demo.bas - Comprehensive API audit for Viper.Collections.Bag
' Tests: New, Put, Has, Drop, Len, IsEmpty, Items, Clear, Common, Diff, Merge

PRINT "=== Bag API Audit ==="

' --- New ---
PRINT "--- New ---"
DIM bag AS OBJECT
bag = Viper.Collections.Bag.New()
PRINT bag.Len       ' 0
PRINT bag.IsEmpty   ' 1

' --- Put / Len ---
PRINT "--- Put / Len ---"
PRINT bag.Put("apple")    ' 1 (new)
PRINT bag.Put("banana")   ' 1 (new)
PRINT bag.Put("cherry")   ' 1 (new)
PRINT bag.Put("apple")    ' 0 (duplicate)
PRINT bag.Len              ' 3
PRINT bag.IsEmpty          ' 0

' --- Has ---
PRINT "--- Has ---"
PRINT bag.Has("apple")    ' 1
PRINT bag.Has("banana")   ' 1
PRINT bag.Has("grape")    ' 0

' --- Drop ---
PRINT "--- Drop ---"
PRINT bag.Drop("banana")  ' 1
PRINT bag.Has("banana")   ' 0
PRINT bag.Len              ' 2
PRINT bag.Drop("banana")  ' 0

' --- Items ---
PRINT "--- Items ---"
DIM items AS OBJECT
items = bag.Items()
PRINT items.Len            ' 2

' --- Merge (union) ---
PRINT "--- Merge ---"
DIM b1 AS OBJECT = Viper.Collections.Bag.New()
b1.Put("a")
b1.Put("b")
b1.Put("c")

DIM b2 AS OBJECT = Viper.Collections.Bag.New()
b2.Put("b")
b2.Put("c")
b2.Put("d")

DIM merged AS OBJECT = b1.Union(b2)
PRINT merged.Len           ' 4
PRINT merged.Has("a")      ' 1
PRINT merged.Has("d")      ' 1

' --- Intersect ---
PRINT "--- Intersect ---"
DIM common AS OBJECT = b1.Intersect(b2)
PRINT common.Len           ' 2
PRINT common.Has("b")      ' 1
PRINT common.Has("c")      ' 1
PRINT common.Has("a")      ' 0

' --- Diff ---
PRINT "--- Diff ---"
DIM diff AS OBJECT = b1.Diff(b2)
PRINT diff.Len             ' 1
PRINT diff.Has("a")        ' 1
PRINT diff.Has("b")        ' 0

' --- Clear ---
PRINT "--- Clear ---"
b1.Clear()
PRINT b1.Len               ' 0
PRINT b1.IsEmpty           ' 1

PRINT "=== Bag audit complete ==="
END
