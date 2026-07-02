' lrucache_demo.bas - Comprehensive API audit for Viper.Collections.LruCache
' Tests: New, Set, Get, Peek, Has, Remove, RemoveOldest, Keys, Values,
'        Len, Capacity, IsEmpty, Clear

PRINT "=== LruCache API Audit ==="

' --- New ---
PRINT "--- New ---"
DIM cache AS OBJECT
cache = Viper.Collections.LruCache.New(3)
PRINT cache.Count       ' 0
PRINT cache.Capacity  ' 3
PRINT cache.IsEmpty   ' 1

' --- Set / Len ---
PRINT "--- Set / Len ---"
cache.Set("a", "alpha")
cache.Set("b", "beta")
cache.Set("c", "gamma")
PRINT cache.Count       ' 3
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

' --- Set (eviction) ---
PRINT "--- Set (eviction) ---"
cache.Set("d", "delta")
PRINT cache.Count         ' 3
PRINT cache.Has("c")    ' 0 (evicted)
PRINT cache.Has("d")    ' 1
PRINT cache.Get("d")    ' delta

' --- Set (update existing) ---
PRINT "--- Set (update) ---"
cache.Set("a", "ALPHA")
PRINT cache.Get("a")    ' ALPHA
PRINT cache.Count          ' 3

' --- Keys ---
PRINT "--- Keys ---"
DIM keys AS OBJECT
keys = cache.Keys()
PRINT keys.Count           ' 3

' --- Values ---
PRINT "--- Values ---"
DIM vals AS OBJECT
vals = cache.Values()
PRINT vals.Count           ' 3

' --- Remove ---
PRINT "--- Remove ---"
PRINT cache.Remove("b")    ' 1
PRINT cache.Has("b")       ' 0
PRINT cache.Count             ' 2
PRINT cache.Remove("b")    ' 0

' --- RemoveOldest ---
PRINT "--- RemoveOldest ---"
cache.Set("e", "epsilon")
PRINT cache.Count             ' 3
PRINT cache.RemoveOldest()  ' 1
PRINT cache.Count             ' 2

' --- Clear ---
PRINT "--- Clear ---"
cache.Clear()
PRINT cache.Count             ' 0
PRINT cache.IsEmpty         ' 1
PRINT cache.Capacity        ' 3

PRINT "=== LruCache audit complete ==="
END
