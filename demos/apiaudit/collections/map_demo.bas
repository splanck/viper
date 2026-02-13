' map_demo.bas - Comprehensive API audit for Viper.Collections.Map
' Tests: New, Set, Get, GetOr, Has, Remove, Clear, Len, IsEmpty,
'        Keys, Values, SetIfMissing

PRINT "=== Map API Audit ==="

' --- New ---
PRINT "--- New ---"
DIM m AS Viper.Collections.Map
m = Viper.Collections.Map.New()
PRINT m.Len       ' 0
PRINT m.IsEmpty   ' 1

' --- Set / Len ---
PRINT "--- Set / Len ---"
m.Set("name", "Alice")
m.Set("age", Viper.Core.Box.I64(30))
m.Set("city", "Boston")
PRINT m.Len       ' 3
PRINT m.IsEmpty   ' 0

' --- Get ---
PRINT "--- Get ---"
PRINT m.Get("name")   ' Alice
PRINT m.Get("city")   ' Boston

' --- Has ---
PRINT "--- Has ---"
PRINT m.Has("name")   ' 1
PRINT m.Has("email")  ' 0

' --- GetOr ---
PRINT "--- GetOr ---"
PRINT m.GetOr("name", "Unknown")   ' Alice
PRINT m.GetOr("email", "N/A")      ' N/A
PRINT m.Has("email")                ' 0 (GetOr does not insert)

' --- SetIfMissing ---
PRINT "--- SetIfMissing ---"
PRINT m.SetIfMissing("name", "Bob")     ' 0 (already exists)
PRINT m.Get("name")                      ' Alice (unchanged)
PRINT m.SetIfMissing("email", "a@b.c")  ' 1 (inserted)
PRINT m.Get("email")                     ' a@b.c
PRINT m.Len                              ' 4

' --- Set (update existing) ---
PRINT "--- Set (update) ---"
m.Set("age", Viper.Core.Box.I64(31))
PRINT m.Len                              ' 4 (no new entry)

' --- Remove ---
PRINT "--- Remove ---"
PRINT m.Remove("city")    ' 1
PRINT m.Has("city")       ' 0
PRINT m.Len               ' 3
PRINT m.Remove("city")    ' 0 (already gone)

' --- Keys ---
PRINT "--- Keys ---"
DIM keys AS OBJECT
keys = m.Keys()
PRINT keys.Len             ' 3

' --- Values ---
PRINT "--- Values ---"
DIM vals AS OBJECT
vals = m.Values()
PRINT vals.Len             ' 3

' --- Clear ---
PRINT "--- Clear ---"
m.Clear()
PRINT m.Len               ' 0
PRINT m.IsEmpty           ' 1

PRINT "=== Map audit complete ==="
END
