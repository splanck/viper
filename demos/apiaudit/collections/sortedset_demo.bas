' sortedset_demo.bas - Comprehensive API audit for Viper.Collections.SortedSet
' Tests: New, Put, Drop, Has, First, Last, Floor, Ceil, Lower, Higher,
'        At, IndexOf, Items, Range, Take, Skip, Merge, Common, Diff,
'        IsSubset, Len, IsEmpty, Clear

PRINT "=== SortedSet API Audit ==="

' --- New ---
PRINT "--- New ---"
DIM ss AS OBJECT
ss = Viper.Collections.SortedSet.New()
PRINT ss.Len       ' 0
PRINT ss.IsEmpty   ' 1

' --- Put ---
PRINT "--- Put ---"
PRINT ss.Put("cherry")     ' 1 (new)
PRINT ss.Put("apple")      ' 1 (new)
PRINT ss.Put("banana")     ' 1 (new)
PRINT ss.Put("date")       ' 1 (new)
PRINT ss.Put("elderberry") ' 1 (new)
PRINT ss.Put("apple")      ' 0 (duplicate)
PRINT ss.Len               ' 5
PRINT ss.IsEmpty           ' 0

' --- Has ---
PRINT "--- Has ---"
PRINT ss.Has("apple")    ' 1
PRINT ss.Has("banana")   ' 1
PRINT ss.Has("fig")      ' 0

' --- First / Last (sorted order) ---
PRINT "--- First / Last ---"
PRINT ss.First()   ' apple
PRINT ss.Last()    ' elderberry

' --- At (index access, sorted) ---
PRINT "--- At ---"
PRINT ss.At(0)  ' apple
PRINT ss.At(1)  ' banana
PRINT ss.At(2)  ' cherry
PRINT ss.At(3)  ' date
PRINT ss.At(4)  ' elderberry

' --- IndexOf ---
PRINT "--- IndexOf ---"
PRINT ss.IndexOf("apple")      ' 0
PRINT ss.IndexOf("cherry")     ' 2
PRINT ss.IndexOf("elderberry") ' 4

' --- Floor (greatest element <= key) ---
PRINT "--- Floor ---"
PRINT ss.Floor("cherry")  ' cherry
PRINT ss.Floor("c")       ' banana

' --- Ceil (smallest element >= key) ---
PRINT "--- Ceil ---"
PRINT ss.Ceil("cherry")   ' cherry
PRINT ss.Ceil("c")        ' cherry

' --- Lower (greatest element < key) ---
PRINT "--- Lower ---"
PRINT ss.Lower("cherry")  ' banana

' --- Higher (smallest element > key) ---
PRINT "--- Higher ---"
PRINT ss.Higher("cherry") ' date

' --- Items ---
PRINT "--- Items ---"
DIM items AS OBJECT
items = ss.Items()
PRINT items.Len  ' 5

' --- Range ---
PRINT "--- Range ---"
DIM ranged AS OBJECT
ranged = ss.Range("banana", "date")
PRINT ranged.Len  ' 3

' --- Take ---
PRINT "--- Take ---"
DIM taken AS OBJECT
taken = ss.Take(3)
PRINT taken.Len  ' 3

' --- Skip ---
PRINT "--- Skip ---"
DIM skipped AS OBJECT
skipped = ss.Skip(2)
PRINT skipped.Len  ' 3

' --- Drop ---
PRINT "--- Drop ---"
PRINT ss.Drop("banana")   ' 1
PRINT ss.Has("banana")    ' 0
PRINT ss.Len              ' 4
PRINT ss.Drop("banana")   ' 0

' --- Merge ---
PRINT "--- Merge ---"
DIM ss2 AS OBJECT
ss2 = Viper.Collections.SortedSet.New()
ss2.Put("apple")
ss2.Put("fig")
ss2.Put("grape")
DIM merged AS OBJECT
merged = ss.Merge(ss2)
PRINT merged.Len  ' 6

' --- Common (intersection) ---
PRINT "--- Common ---"
DIM common AS OBJECT
common = ss.Common(ss2)
PRINT common.Len  ' 1

' --- Diff ---
PRINT "--- Diff ---"
DIM diff AS OBJECT
diff = ss.Diff(ss2)
PRINT diff.Len  ' 3

' --- IsSubset ---
PRINT "--- IsSubset ---"
DIM sub1 AS OBJECT
sub1 = Viper.Collections.SortedSet.New()
sub1.Put("apple")
sub1.Put("cherry")
PRINT sub1.IsSubset(ss)  ' 1

' --- Clear ---
PRINT "--- Clear ---"
ss.Clear()
PRINT ss.Len       ' 0
PRINT ss.IsEmpty   ' 1

PRINT "=== SortedSet audit complete ==="
END
