' =============================================================================
' API Audit: Zanna.Threads.ConcurrentMap (BASIC)
' =============================================================================
' Tests: New, Set, Get, GetOr, Has, SetIfMissing, Remove, Clear, Keys, Values,
'        Len, IsEmpty
' =============================================================================

PRINT "=== API Audit: Zanna.Threads.ConcurrentMap ==="

' --- New ---
PRINT "--- New ---"
DIM m AS OBJECT = Zanna.Threads.ConcurrentMap.New()
PRINT "Created map"

' --- IsEmpty (initial) ---
PRINT "--- IsEmpty (initial) ---"
PRINT "IsEmpty: "; m.IsEmpty

' --- Len (initial) ---
PRINT "--- Len (initial) ---"
PRINT "Len: "; m.Count

' --- Set ---
PRINT "--- Set ---"
m.Set("name", Zanna.Core.Box.Str("Alice"))
m.Set("age", Zanna.Core.Box.I64(30))
m.Set("city", Zanna.Core.Box.Str("Paris"))
PRINT "Set 3 entries"

' --- Len (after set) ---
PRINT "--- Len (after set) ---"
PRINT "Len: "; m.Count

' --- IsEmpty (after set) ---
PRINT "--- IsEmpty (after set) ---"
PRINT "IsEmpty: "; m.IsEmpty

' --- Get ---
PRINT "--- Get ---"
DIM nameVal AS OBJECT = m.Get("name")
PRINT "Get(name): "; Zanna.Core.Box.ToStr(nameVal)
DIM ageVal AS OBJECT = m.Get("age")
PRINT "Get(age): "; Zanna.Core.Box.ToI64(ageVal)

' --- Get (missing key) ---
PRINT "--- Get (missing key) ---"
DIM missingVal AS OBJECT = m.Get("phone")
PRINT "Get(phone) returned (expect empty for null)"

' --- GetOr ---
PRINT "--- GetOr ---"
DIM cityVal AS OBJECT = m.GetOr("city", Zanna.Core.Box.Str("Unknown"))
PRINT "GetOr(city, Unknown): "; Zanna.Core.Box.ToStr(cityVal)
DIM phoneVal AS OBJECT = m.GetOr("phone", Zanna.Core.Box.Str("N/A"))
PRINT "GetOr(phone, N/A): "; Zanna.Core.Box.ToStr(phoneVal)

' --- Has ---
PRINT "--- Has ---"
PRINT "Has(name): "; m.Has("name")
PRINT "Has(phone): "; m.Has("phone")

' --- SetIfMissing ---
PRINT "--- SetIfMissing ---"
DIM ins1 AS INTEGER = m.SetIfMissing("name", Zanna.Core.Box.Str("Bob"))
PRINT "SetIfMissing(name, Bob): "; ins1
PRINT "Get(name) still: "; Zanna.Core.Box.ToStr(m.Get("name"))

DIM ins2 AS INTEGER = m.SetIfMissing("email", Zanna.Core.Box.Str("alice@test.com"))
PRINT "SetIfMissing(email, ...): "; ins2
PRINT "Get(email): "; Zanna.Core.Box.ToStr(m.Get("email"))

' --- Keys ---
PRINT "--- Keys ---"
DIM keys AS Zanna.Collections.Seq = m.Keys()
PRINT "Keys count: "; keys.Count

' --- Values ---
PRINT "--- Values ---"
DIM vals AS Zanna.Collections.Seq = m.Values()
PRINT "Values count: "; vals.Count

' --- Remove ---
PRINT "--- Remove ---"
DIM removed AS INTEGER = m.Remove("email")
PRINT "Remove(email): "; removed
PRINT "Has(email) after remove: "; m.Has("email")
DIM removeMissing AS INTEGER = m.Remove("nonexistent")
PRINT "Remove(nonexistent): "; removeMissing
PRINT "Len after remove: "; m.Count

' --- Clear ---
PRINT "--- Clear ---"
m.Clear()
PRINT "Len after Clear: "; m.Count
PRINT "IsEmpty after Clear: "; m.IsEmpty

PRINT "=== ConcurrentMap Audit Complete ==="
END
