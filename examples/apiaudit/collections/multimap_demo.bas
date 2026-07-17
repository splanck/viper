' multimap_demo.bas - Comprehensive API audit for Zanna.Collections.MultiMap
' Tests: New, Add, Get, GetFirst, Has, RemoveAll, Keys, Len, IsEmpty,
'        KeyCount, CountFor, Clear

PRINT "=== MultiMap API Audit ==="

' --- New ---
PRINT "--- New ---"
DIM mm AS OBJECT
mm = Zanna.Collections.MultiMap.New()
PRINT mm.Count        ' 0
PRINT mm.IsEmpty    ' 1

' --- Add / Len ---
PRINT "--- Add / Len ---"
mm.Add("color", "red")
mm.Add("color", "green")
mm.Add("color", "blue")
mm.Add("size", "small")
mm.Add("size", "large")
PRINT mm.Count        ' 5
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
DIM colors AS Zanna.Collections.Seq
colors = mm.Get("color")
PRINT colors.Count         ' 3
PRINT colors.Get(0)      ' red
PRINT colors.Get(1)      ' green
PRINT colors.Get(2)      ' blue

DIM sizes AS Zanna.Collections.Seq
sizes = mm.Get("size")
PRINT sizes.Count          ' 2
PRINT sizes.Get(0)       ' small
PRINT sizes.Get(1)       ' large

DIM empty AS Zanna.Collections.Seq
empty = mm.Get("shape")
PRINT empty.Count          ' 0

' --- GetFirst ---
PRINT "--- GetFirst ---"
PRINT mm.GetFirst("color")   ' red
PRINT mm.GetFirst("size")    ' small

' --- Keys ---
PRINT "--- Keys ---"
DIM keys AS OBJECT
keys = mm.Keys()
PRINT keys.Count            ' 2

' --- RemoveAll ---
PRINT "--- RemoveAll ---"
PRINT mm.RemoveAll("color")   ' 1
PRINT mm.Has("color")         ' 0
PRINT mm.Count                   ' 2
PRINT mm.KeyCount              ' 1
PRINT mm.RemoveAll("color")   ' 0

' --- Clear ---
PRINT "--- Clear ---"
mm.Clear()
PRINT mm.Count        ' 0
PRINT mm.IsEmpty    ' 1
PRINT mm.KeyCount   ' 0

PRINT "=== MultiMap audit complete ==="
END
