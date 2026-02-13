' defaultmap_demo.bas - Comprehensive API audit for Viper.Collections.DefaultMap
' Tests: New(default_value), Get, Set, Has, Remove, Keys, GetDefault, Clear, Len

PRINT "=== DefaultMap API Audit ==="

' --- New ---
PRINT "--- New ---"
DIM dm AS OBJECT
dm = Viper.Collections.DefaultMap.New(Viper.Core.Box.Str("N/A"))
PRINT dm.Len       ' 0

' --- Set / Len ---
PRINT "--- Set / Len ---"
dm.Set("name", "Alice")
dm.Set("city", "Boston")
dm.Set("role", "Engineer")
PRINT dm.Len       ' 3

' --- Get (existing key) ---
PRINT "--- Get (existing) ---"
PRINT dm.Get("name")   ' Alice
PRINT dm.Get("city")   ' Boston

' --- Get (missing key returns default) ---
PRINT "--- Get (missing) ---"
PRINT dm.Get("email")  ' N/A
PRINT dm.Get("phone")  ' N/A

' --- Has ---
PRINT "--- Has ---"
PRINT dm.Has("name")    ' 1
PRINT dm.Has("email")   ' 0

' --- GetDefault ---
PRINT "--- GetDefault ---"
PRINT dm.GetDefault()   ' N/A

' --- Set (update existing) ---
PRINT "--- Set (update) ---"
dm.Set("name", "Bob")
PRINT dm.Get("name")   ' Bob
PRINT dm.Len            ' 3

' --- Keys ---
PRINT "--- Keys ---"
DIM keys AS OBJECT
keys = dm.Keys()
PRINT keys.Len          ' 3

' --- Remove ---
PRINT "--- Remove ---"
PRINT dm.Remove("city")   ' 1
PRINT dm.Has("city")      ' 0
PRINT dm.Len               ' 2
PRINT dm.Remove("city")   ' 0
PRINT dm.Get("city")      ' N/A

' --- Clear ---
PRINT "--- Clear ---"
dm.Clear()
PRINT dm.Len               ' 0
PRINT dm.Get("name")      ' N/A

' --- New with integer default ---
PRINT "--- New (integer default) ---"
DIM dm2 AS OBJECT
dm2 = Viper.Collections.DefaultMap.New(Viper.Core.Box.I64(0))
dm2.Set("count", Viper.Core.Box.I64(42))
PRINT Viper.Core.Box.ToI64(dm2.Get("count"))    ' 42
PRINT Viper.Core.Box.ToI64(dm2.Get("missing"))  ' 0

PRINT "=== DefaultMap audit complete ==="
END
