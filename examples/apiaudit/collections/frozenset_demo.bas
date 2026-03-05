' frozenset_demo.bas - Comprehensive API audit for Viper.Collections.FrozenSet
' Tests: Empty, FromSeq, Len, IsEmpty, Has, Items, Union, Intersect,
'        Diff, IsSubset, Equals

PRINT "=== FrozenSet API Audit ==="

' --- Empty ---
PRINT "--- Empty ---"
DIM empty AS OBJECT
empty = Viper.Collections.FrozenSet.Empty()
PRINT empty.Len       ' 0
PRINT empty.IsEmpty   ' 1

' --- FromSeq ---
PRINT "--- FromSeq ---"
DIM items AS Viper.Collections.Seq
items = Viper.Collections.Seq.New()
items.Push("apple")
items.Push("banana")
items.Push("cherry")
items.Push("apple")  ' duplicate
DIM fs AS OBJECT
fs = Viper.Collections.FrozenSet.FromSeq(items)
PRINT fs.Len          ' 3
PRINT fs.IsEmpty      ' 0

' --- Has ---
PRINT "--- Has ---"
PRINT fs.Has("apple")    ' 1
PRINT fs.Has("banana")   ' 1
PRINT fs.Has("grape")    ' 0

' --- Items ---
PRINT "--- Items ---"
DIM all AS OBJECT
all = fs.Items()
PRINT all.Len             ' 3

' --- Union ---
PRINT "--- Union ---"
DIM items2 AS Viper.Collections.Seq
items2 = Viper.Collections.Seq.New()
items2.Push("cherry")
items2.Push("date")
items2.Push("elderberry")
DIM fs2 AS OBJECT
fs2 = Viper.Collections.FrozenSet.FromSeq(items2)
DIM united AS OBJECT
united = fs.Union(fs2)
PRINT united.Len          ' 5

' --- Intersect ---
PRINT "--- Intersect ---"
DIM inter AS OBJECT
inter = fs.Intersect(fs2)
PRINT inter.Len           ' 1
PRINT inter.Has("cherry") ' 1

' --- Diff ---
PRINT "--- Diff ---"
DIM diff AS OBJECT
diff = fs.Diff(fs2)
PRINT diff.Len            ' 2
PRINT diff.Has("apple")   ' 1
PRINT diff.Has("cherry")  ' 0

' --- IsSubset ---
PRINT "--- IsSubset ---"
DIM subItems AS Viper.Collections.Seq
subItems = Viper.Collections.Seq.New()
subItems.Push("apple")
subItems.Push("banana")
DIM subset AS OBJECT
subset = Viper.Collections.FrozenSet.FromSeq(subItems)
PRINT subset.IsSubset(fs)    ' 1
PRINT fs.IsSubset(subset)    ' 0

' --- Equals ---
PRINT "--- Equals ---"
DIM items3 AS Viper.Collections.Seq
items3 = Viper.Collections.Seq.New()
items3.Push("banana")
items3.Push("cherry")
items3.Push("apple")
DIM fs3 AS OBJECT
fs3 = Viper.Collections.FrozenSet.FromSeq(items3)
PRINT fs.Equals(fs3)      ' 1
PRINT fs.Equals(fs2)      ' 0

' --- Empty equals empty ---
PRINT "--- Empty equals empty ---"
DIM e1 AS OBJECT = Viper.Collections.FrozenSet.Empty()
DIM e2 AS OBJECT = Viper.Collections.FrozenSet.Empty()
PRINT e1.Equals(e2)       ' 1

PRINT "=== FrozenSet audit complete ==="
END
