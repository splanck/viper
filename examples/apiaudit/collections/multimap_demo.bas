' multimap_demo.bas - Comprehensive API audit for Viper.Collections.MultiMap
' Tests: New, Put, Get, GetFirst, Has, RemoveAll, Keys, Len, IsEmpty,
'        KeyCount, CountFor, Clear

PRINT "=== MultiMap API Audit ==="

' --- New ---
PRINT "--- New ---"
DIM mm AS OBJECT
mm = Viper.Collections.MultiMap.New()
PRINT mm.Len        ' 0
PRINT mm.IsEmpty    ' 1

' --- Put / Len ---
PRINT "--- Put / Len ---"
mm.Put("color", "red")
mm.Put("color", "green")
mm.Put("color", "blue")
mm.Put("size", "small")
mm.Put("size", "large")
PRINT mm.Len        ' 5
PRINT mm.IsEmpty    ' 0

' --- KeyCount ---
PRINT "--- KeyCount ---"
PRINT mm.KeyCount   ' 2

' --- CountFor ---
PRINT "--- CountFor ---"
PRINT mm.CountFor("color")  ' 3
PRINT mm.CountFor("size")   ' 2
PRINT mm.CountFor("shape")  ' 0

' --- Has ---
PRINT "--- Has ---"
PRINT mm.Has("color")   ' 1
PRINT mm.Has("size")    ' 1
PRINT mm.Has("shape")   ' 0

' --- Get (returns Seq of all values) ---
PRINT "--- Get ---"
DIM colors AS Viper.Collections.Seq
colors = mm.Get("color")
PRINT colors.Len         ' 3
PRINT colors.Get(0)      ' red
PRINT colors.Get(1)      ' green
PRINT colors.Get(2)      ' blue

DIM sizes AS Viper.Collections.Seq
sizes = mm.Get("size")
PRINT sizes.Len          ' 2
PRINT sizes.Get(0)       ' small
PRINT sizes.Get(1)       ' large

DIM empty AS Viper.Collections.Seq
empty = mm.Get("shape")
PRINT empty.Len          ' 0

' --- GetFirst ---
PRINT "--- GetFirst ---"
PRINT mm.GetFirst("color")   ' red
PRINT mm.GetFirst("size")    ' small

' --- Keys ---
PRINT "--- Keys ---"
DIM keys AS OBJECT
keys = mm.Keys()
PRINT keys.Len            ' 2

' --- RemoveAll ---
PRINT "--- RemoveAll ---"
PRINT mm.RemoveAll("color")   ' 1
PRINT mm.Has("color")         ' 0
PRINT mm.Len                   ' 2
PRINT mm.KeyCount              ' 1
PRINT mm.RemoveAll("color")   ' 0

' --- Clear ---
PRINT "--- Clear ---"
mm.Clear()
PRINT mm.Len        ' 0
PRINT mm.IsEmpty    ' 1
PRINT mm.KeyCount   ' 0

PRINT "=== MultiMap audit complete ==="
END
