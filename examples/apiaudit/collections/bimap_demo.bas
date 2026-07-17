' bimap_demo.bas - Comprehensive API audit for Zanna.Collections.BiMap
' Tests: New, Set, GetByKey, GetByValue, HasKey, HasValue,
'        RemoveByKey, RemoveByValue, Keys, Values, Len, IsEmpty, Clear

PRINT "=== BiMap API Audit ==="

' --- New ---
PRINT "--- New ---"
DIM bm AS OBJECT
bm = Zanna.Collections.BiMap.New()
PRINT bm.Count       ' 0
PRINT bm.IsEmpty   ' 1

' --- Set / Len ---
PRINT "--- Set / Len ---"
bm.Set("us", "dollar")
bm.Set("uk", "pound")
bm.Set("jp", "yen")
bm.Set("eu", "euro")
PRINT bm.Count       ' 4
PRINT bm.IsEmpty   ' 0

' --- GetByKey ---
PRINT "--- GetByKey ---"
PRINT bm.GetByKey("us")   ' dollar
PRINT bm.GetByKey("uk")   ' pound
PRINT bm.GetByKey("jp")   ' yen

' --- GetByValue (inverse lookup) ---
PRINT "--- GetByValue ---"
PRINT bm.GetByValue("dollar")  ' us
PRINT bm.GetByValue("pound")   ' uk
PRINT bm.GetByValue("yen")     ' jp

' --- HasKey / HasValue ---
PRINT "--- HasKey / HasValue ---"
PRINT bm.HasKey("us")        ' 1
PRINT bm.HasKey("ca")        ' 0
PRINT bm.HasValue("euro")    ' 1
PRINT bm.HasValue("rupee")   ' 0

' --- Set (update) ---
PRINT "--- Set (update) ---"
bm.Set("us", "greenback")
PRINT bm.GetByKey("us")           ' greenback
PRINT bm.GetByValue("greenback")  ' us
PRINT bm.HasValue("dollar")       ' 0
PRINT bm.Count                      ' 4

' --- RemoveByKey ---
PRINT "--- RemoveByKey ---"
PRINT bm.RemoveByKey("jp")    ' 1
PRINT bm.HasKey("jp")         ' 0
PRINT bm.HasValue("yen")      ' 0
PRINT bm.Count                  ' 3
PRINT bm.RemoveByKey("jp")    ' 0

' --- RemoveByValue ---
PRINT "--- RemoveByValue ---"
PRINT bm.RemoveByValue("euro")  ' 1
PRINT bm.HasKey("eu")           ' 0
PRINT bm.HasValue("euro")       ' 0
PRINT bm.Count                    ' 2

' --- Keys ---
PRINT "--- Keys ---"
DIM keys AS OBJECT
keys = bm.Keys()
PRINT keys.Count   ' 2

' --- Values ---
PRINT "--- Values ---"
DIM vals AS OBJECT
vals = bm.Values()
PRINT vals.Count   ' 2

' --- Clear ---
PRINT "--- Clear ---"
bm.Clear()
PRINT bm.Count       ' 0
PRINT bm.IsEmpty   ' 1

PRINT "=== BiMap audit complete ==="
END
