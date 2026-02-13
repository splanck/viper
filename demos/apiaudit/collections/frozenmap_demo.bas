' frozenmap_demo.bas - Comprehensive API audit for Viper.Collections.FrozenMap
' Tests: Empty, FromSeqs, Len, IsEmpty, Get, Has, Keys, Values,
'        GetOr, Merge, Equals

PRINT "=== FrozenMap API Audit ==="

' --- Empty ---
PRINT "--- Empty ---"
DIM empty AS OBJECT
empty = Viper.Collections.FrozenMap.Empty()
PRINT empty.Len       ' 0
PRINT empty.IsEmpty   ' 1

' --- FromSeqs ---
PRINT "--- FromSeqs ---"
DIM keys AS Viper.Collections.Seq
keys = Viper.Collections.Seq.New()
keys.Push("name")
keys.Push("age")
keys.Push("city")

DIM vals AS Viper.Collections.Seq
vals = Viper.Collections.Seq.New()
vals.Push("Alice")
vals.Push(Viper.Core.Box.I64(30))
vals.Push("Boston")

DIM fm AS OBJECT
fm = Viper.Collections.FrozenMap.FromSeqs(keys, vals)
PRINT fm.Len          ' 3
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
PRINT fmKeys.Len        ' 3

' --- Values ---
PRINT "--- Values ---"
DIM fmVals AS OBJECT
fmVals = fm.Values()
PRINT fmVals.Len        ' 3

' --- GetOr ---
PRINT "--- GetOr ---"
PRINT fm.GetOr("name", "Unknown")   ' Alice
PRINT fm.GetOr("email", "N/A")      ' N/A

' --- Merge ---
PRINT "--- Merge ---"
DIM keys2 AS Viper.Collections.Seq
keys2 = Viper.Collections.Seq.New()
keys2.Push("city")
keys2.Push("email")

DIM vals2 AS Viper.Collections.Seq
vals2 = Viper.Collections.Seq.New()
vals2.Push("NYC")
vals2.Push("a@b.com")

DIM fm2 AS OBJECT
fm2 = Viper.Collections.FrozenMap.FromSeqs(keys2, vals2)
DIM merged AS OBJECT
merged = fm.Merge(fm2)
PRINT merged.Len            ' 4
PRINT merged.Get("city")    ' NYC
PRINT merged.Get("email")   ' a@b.com
PRINT merged.Get("name")    ' Alice

' --- Equals ---
PRINT "--- Equals ---"
DIM keys3 AS Viper.Collections.Seq
keys3 = Viper.Collections.Seq.New()
keys3.Push("age")
keys3.Push("name")
keys3.Push("city")

DIM vals3 AS Viper.Collections.Seq
vals3 = Viper.Collections.Seq.New()
vals3.Push(Viper.Core.Box.I64(30))
vals3.Push("Alice")
vals3.Push("Boston")

DIM fm3 AS OBJECT
fm3 = Viper.Collections.FrozenMap.FromSeqs(keys3, vals3)
PRINT fm.Equals(fm3)        ' 1
PRINT fm.Equals(fm2)        ' 0

' --- Empty equals empty ---
PRINT "--- Empty equals empty ---"
DIM e1 AS OBJECT = Viper.Collections.FrozenMap.Empty()
DIM e2 AS OBJECT = Viper.Collections.FrozenMap.Empty()
PRINT e1.Equals(e2)         ' 1

PRINT "=== FrozenMap audit complete ==="
END
