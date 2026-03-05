' weakmap_demo.bas - Comprehensive API audit for Viper.Collections.WeakMap
' Tests: New, Set, Get, Has, Remove, Keys, Len, IsEmpty, Clear, Compact

PRINT "=== WeakMap API Audit ==="

' --- New ---
PRINT "--- New ---"
DIM wm AS OBJECT
wm = Viper.Collections.WeakMap.New()
PRINT wm.Len       ' 0
PRINT wm.IsEmpty   ' 1

' --- Set / Len ---
PRINT "--- Set / Len ---"
DIM obj1 AS OBJECT = Viper.Core.Box.Str("value1")
DIM obj2 AS OBJECT = Viper.Core.Box.Str("value2")
DIM obj3 AS OBJECT = Viper.Core.Box.Str("value3")
wm.Set("key1", obj1)
wm.Set("key2", obj2)
wm.Set("key3", obj3)
PRINT wm.Len       ' 3
PRINT wm.IsEmpty   ' 0

' --- Get ---
PRINT "--- Get ---"
PRINT Viper.Core.Box.ToStr(wm.Get("key1"))  ' value1
PRINT Viper.Core.Box.ToStr(wm.Get("key2"))  ' value2
PRINT Viper.Core.Box.ToStr(wm.Get("key3"))  ' value3

' --- Has ---
PRINT "--- Has ---"
PRINT wm.Has("key1")    ' 1
PRINT wm.Has("key4")    ' 0

' --- Set (update existing) ---
PRINT "--- Set (update) ---"
DIM obj1b AS OBJECT = Viper.Core.Box.Str("VALUE1")
wm.Set("key1", obj1b)
PRINT Viper.Core.Box.ToStr(wm.Get("key1"))  ' VALUE1
PRINT wm.Len                                 ' 3

' --- Keys ---
PRINT "--- Keys ---"
DIM keys AS OBJECT
keys = wm.Keys()
PRINT keys.Len           ' 3

' --- Remove ---
PRINT "--- Remove ---"
PRINT wm.Remove("key2")   ' 1
PRINT wm.Has("key2")      ' 0
PRINT wm.Len              ' 2
PRINT wm.Remove("key2")   ' 0

' --- Compact ---
PRINT "--- Compact ---"
DIM removed AS INTEGER
removed = wm.Compact()
PRINT wm.Len              ' 2

' --- Clear ---
PRINT "--- Clear ---"
wm.Clear()
PRINT wm.Len              ' 0
PRINT wm.IsEmpty          ' 1

PRINT "=== WeakMap audit complete ==="
END
