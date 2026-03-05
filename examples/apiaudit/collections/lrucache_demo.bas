' lrucache_demo.bas - Comprehensive API audit for Viper.Collections.LruCache
' Tests: New, Put, Get, Peek, Has, Remove, RemoveOldest, Keys, Values,
'        Len, Cap, IsEmpty, Clear

PRINT "=== LruCache API Audit ==="

' --- New ---
PRINT "--- New ---"
DIM cache AS OBJECT
cache = Viper.Collections.LruCache.New(3)
PRINT cache.Len       ' 0
PRINT cache.Cap       ' 3
PRINT cache.IsEmpty   ' 1

' --- Put / Len ---
PRINT "--- Put / Len ---"
cache.Put("a", "alpha")
cache.Put("b", "beta")
cache.Put("c", "gamma")
PRINT cache.Len       ' 3
PRINT cache.IsEmpty   ' 0

' --- Get ---
PRINT "--- Get ---"
PRINT cache.Get("a")   ' alpha
PRINT cache.Get("b")   ' beta

' --- Has ---
PRINT "--- Has ---"
PRINT cache.Has("a")   ' 1
PRINT cache.Has("x")   ' 0

' --- Peek (does not promote) ---
PRINT "--- Peek ---"
PRINT cache.Peek("c")  ' gamma

' --- Put (eviction) ---
PRINT "--- Put (eviction) ---"
cache.Put("d", "delta")
PRINT cache.Len         ' 3
PRINT cache.Has("c")    ' 0 (evicted)
PRINT cache.Has("d")    ' 1
PRINT cache.Get("d")    ' delta

' --- Put (update existing) ---
PRINT "--- Put (update) ---"
cache.Put("a", "ALPHA")
PRINT cache.Get("a")    ' ALPHA
PRINT cache.Len          ' 3

' --- Keys ---
PRINT "--- Keys ---"
DIM keys AS OBJECT
keys = cache.Keys()
PRINT keys.Len           ' 3

' --- Values ---
PRINT "--- Values ---"
DIM vals AS OBJECT
vals = cache.Values()
PRINT vals.Len           ' 3

' --- Remove ---
PRINT "--- Remove ---"
PRINT cache.Remove("b")    ' 1
PRINT cache.Has("b")       ' 0
PRINT cache.Len             ' 2
PRINT cache.Remove("b")    ' 0

' --- RemoveOldest ---
PRINT "--- RemoveOldest ---"
cache.Put("e", "epsilon")
PRINT cache.Len             ' 3
PRINT cache.RemoveOldest()  ' 1
PRINT cache.Len             ' 2

' --- Clear ---
PRINT "--- Clear ---"
cache.Clear()
PRINT cache.Len             ' 0
PRINT cache.IsEmpty         ' 1
PRINT cache.Cap             ' 3

PRINT "=== LruCache audit complete ==="
END
