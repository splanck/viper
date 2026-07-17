' frozenmap_demo.bas - Comprehensive API audit for Zanna.Collections.FrozenMap
' Tests: Empty, FromSeqs, Len, IsEmpty, Get, Has, Keys, Values,
'        GetOr, Merge, Equals

PRINT "=== FrozenMap API Audit ==="

' --- Empty ---
PRINT "--- Empty ---"
DIM empty AS OBJECT
empty = Zanna.Collections.FrozenMap.Empty()
PRINT empty.Count       ' 0
PRINT empty.IsEmpty   ' 1

' --- FromSeqs ---
PRINT "--- FromSeqs ---"
DIM keys AS Zanna.Collections.Seq
keys = Zanna.Collections.Seq.New()
keys.Push("name")
keys.Push("age")
keys.Push("city")

DIM vals AS Zanna.Collections.Seq
vals = Zanna.Collections.Seq.New()
vals.Push("Alice")
vals.Push(Zanna.Core.Box.I64(30))
vals.Push("Boston")

DIM fm AS OBJECT
fm = Zanna.Collections.FrozenMap.FromSeqs(keys, vals)
PRINT fm.Count          ' 3
PRINT fm.IsEmpty      ' 0

' --- Get ---
PRINT "--- Get ---"
PRINT fm.Get("name")   ' Alice
PRINT fm.Get("city")   ' Boston

' --- Has ---
PRINT "--- Has ---"
PRINT fm.Has("name")    ' 1
PRINT fm.Has("email")   ' 0

' --- Keys ---
PRINT "--- Keys ---"
DIM fmKeys AS OBJECT
fmKeys = fm.Keys()
PRINT fmKeys.Count        ' 3

' --- Values ---
PRINT "--- Values ---"
DIM fmVals AS OBJECT
fmVals = fm.Values()
PRINT fmVals.Count        ' 3

' --- GetOr ---
PRINT "--- GetOr ---"
PRINT fm.GetOr("name", "Unknown")   ' Alice
PRINT fm.GetOr("email", "N/A")      ' N/A

' --- Merge ---
PRINT "--- Merge ---"
DIM keys2 AS Zanna.Collections.Seq
keys2 = Zanna.Collections.Seq.New()
keys2.Push("city")
keys2.Push("email")

DIM vals2 AS Zanna.Collections.Seq
vals2 = Zanna.Collections.Seq.New()
vals2.Push("NYC")
vals2.Push("a@b.com")

DIM fm2 AS OBJECT
fm2 = Zanna.Collections.FrozenMap.FromSeqs(keys2, vals2)
DIM merged AS OBJECT
merged = fm.Merge(fm2)
PRINT merged.Count            ' 4
PRINT merged.Get("city")    ' NYC
PRINT merged.Get("email")   ' a@b.com
PRINT merged.Get("name")    ' Alice

' --- Equals ---
PRINT "--- Equals ---"
DIM keys3 AS Zanna.Collections.Seq
keys3 = Zanna.Collections.Seq.New()
keys3.Push("age")
keys3.Push("name")
keys3.Push("city")

DIM vals3 AS Zanna.Collections.Seq
vals3 = Zanna.Collections.Seq.New()
vals3.Push(Zanna.Core.Box.I64(30))
vals3.Push("Alice")
vals3.Push("Boston")

DIM fm3 AS OBJECT
fm3 = Zanna.Collections.FrozenMap.FromSeqs(keys3, vals3)
PRINT fm.Equals(fm3)        ' 1
PRINT fm.Equals(fm2)        ' 0

' --- Empty equals empty ---
PRINT "--- Empty equals empty ---"
DIM e1 AS OBJECT = Zanna.Collections.FrozenMap.Empty()
DIM e2 AS OBJECT = Zanna.Collections.FrozenMap.Empty()
PRINT e1.Equals(e2)         ' 1

PRINT "=== FrozenMap audit complete ==="
END
