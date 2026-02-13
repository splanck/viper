' orderedmap_demo.bas - Comprehensive API audit for Viper.Collections.OrderedMap
' Tests: New, Set, Get, Has, Remove, Clear, Len, IsEmpty, Keys, Values, KeyAt

PRINT "=== OrderedMap API Audit ==="

' --- New ---
PRINT "--- New ---"
DIM om AS OBJECT
om = Viper.Collections.OrderedMap.New()
PRINT om.Len       ' 0
PRINT om.IsEmpty   ' 1

' --- Set / Len ---
PRINT "--- Set / Len ---"
om.Set("first", "alpha")
om.Set("second", "beta")
om.Set("third", "gamma")
PRINT om.Len       ' 3
PRINT om.IsEmpty   ' 0

' --- Get ---
PRINT "--- Get ---"
PRINT om.Get("first")    ' alpha
PRINT om.Get("second")   ' beta
PRINT om.Get("third")    ' gamma

' --- Has ---
PRINT "--- Has ---"
PRINT om.Has("first")    ' 1
PRINT om.Has("fourth")   ' 0

' --- Set (update existing) ---
PRINT "--- Set (update) ---"
om.Set("second", "BETA")
PRINT om.Get("second")   ' BETA
PRINT om.Len              ' 3

' --- KeyAt (insertion order) ---
PRINT "--- KeyAt ---"
PRINT om.KeyAt(0)         ' first
PRINT om.KeyAt(1)         ' second
PRINT om.KeyAt(2)         ' third

' --- Keys (insertion order) ---
PRINT "--- Keys ---"
DIM keys AS OBJECT
keys = om.Keys()
PRINT keys.Len            ' 3
PRINT keys.Get(0)         ' first
PRINT keys.Get(1)         ' second
PRINT keys.Get(2)         ' third

' --- Values (insertion order) ---
PRINT "--- Values ---"
DIM vals AS OBJECT
vals = om.Values()
PRINT vals.Len            ' 3
PRINT vals.Get(0)         ' alpha
PRINT vals.Get(1)         ' BETA
PRINT vals.Get(2)         ' gamma

' --- Remove ---
PRINT "--- Remove ---"
PRINT om.Remove("second")   ' 1
PRINT om.Has("second")      ' 0
PRINT om.Len                 ' 2
PRINT om.Remove("second")   ' 0

' --- Keys after remove ---
PRINT "--- Keys after remove ---"
DIM keys2 AS OBJECT
keys2 = om.Keys()
PRINT keys2.Len              ' 2
PRINT keys2.Get(0)           ' first
PRINT keys2.Get(1)           ' third

' --- Clear ---
PRINT "--- Clear ---"
om.Clear()
PRINT om.Len                 ' 0
PRINT om.IsEmpty             ' 1

PRINT "=== OrderedMap audit complete ==="
END
