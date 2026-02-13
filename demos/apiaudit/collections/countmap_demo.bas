' countmap_demo.bas - Comprehensive API audit for Viper.Collections.CountMap
' Tests: New, Inc, IncBy, Dec, Get, Set, Has, Total, Keys,
'        MostCommon, Remove, Clear, Len, IsEmpty

PRINT "=== CountMap API Audit ==="

' --- New ---
PRINT "--- New ---"
DIM cm AS OBJECT
cm = Viper.Collections.CountMap.New()
PRINT cm.Len       ' 0
PRINT cm.IsEmpty   ' 1

' --- Inc / Len ---
PRINT "--- Inc / Len ---"
PRINT cm.Inc("apple")     ' 1
PRINT cm.Inc("apple")     ' 2
PRINT cm.Inc("banana")    ' 1
PRINT cm.Inc("cherry")    ' 1
PRINT cm.Inc("cherry")    ' 2
PRINT cm.Inc("cherry")    ' 3
PRINT cm.Len               ' 3
PRINT cm.IsEmpty           ' 0

' --- IncBy ---
PRINT "--- IncBy ---"
PRINT cm.IncBy("banana", 5)  ' 6

' --- Get ---
PRINT "--- Get ---"
PRINT cm.Get("apple")    ' 2
PRINT cm.Get("banana")   ' 6
PRINT cm.Get("cherry")   ' 3
PRINT cm.Get("grape")    ' 0

' --- Has ---
PRINT "--- Has ---"
PRINT cm.Has("apple")    ' 1
PRINT cm.Has("grape")    ' 0

' --- Set ---
PRINT "--- Set ---"
cm.Set("date", 10)
PRINT cm.Get("date")     ' 10
PRINT cm.Len              ' 4

' --- Total ---
PRINT "--- Total ---"
PRINT cm.Total            ' 21

' --- Dec ---
PRINT "--- Dec ---"
PRINT cm.Dec("apple")    ' 1
PRINT cm.Dec("apple")    ' 0 (removed)
PRINT cm.Has("apple")    ' 0
PRINT cm.Len              ' 3

' --- Keys ---
PRINT "--- Keys ---"
DIM keys AS OBJECT
keys = cm.Keys()
PRINT keys.Len            ' 3

' --- MostCommon ---
PRINT "--- MostCommon ---"
DIM top AS OBJECT
top = cm.MostCommon(2)
PRINT top.Len             ' 2

' --- Remove ---
PRINT "--- Remove ---"
PRINT cm.Remove("date")    ' 1
PRINT cm.Has("date")       ' 0
PRINT cm.Len                ' 2
PRINT cm.Remove("date")    ' 0

' --- Clear ---
PRINT "--- Clear ---"
cm.Clear()
PRINT cm.Len                ' 0
PRINT cm.IsEmpty            ' 1

PRINT "=== CountMap audit complete ==="
END
