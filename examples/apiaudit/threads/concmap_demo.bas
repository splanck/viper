' =============================================================================
' API Audit: Viper.Threads.ConcurrentMap (BASIC)
' =============================================================================
' Tests: New, Set, Get, GetOr, Has, SetIfMissing, Remove, Clear, Keys, Values,
'        Len, IsEmpty
' =============================================================================

PRINT "=== API Audit: Viper.Threads.ConcurrentMap ==="

' --- New ---
PRINT "--- New ---"
DIM m AS OBJECT = Viper.Threads.ConcurrentMap.New()
PRINT "Created map"

' --- IsEmpty (initial) ---
PRINT "--- IsEmpty (initial) ---"
PRINT "IsEmpty: "; m.IsEmpty

' --- Len (initial) ---
PRINT "--- Len (initial) ---"
PRINT "Len: "; m.Len

' --- Set ---
PRINT "--- Set ---"
m.Set("name", Viper.Core.Box.Str("Alice"))
m.Set("age", Viper.Core.Box.I64(30))
m.Set("city", Viper.Core.Box.Str("Paris"))
PRINT "Set 3 entries"

' --- Len (after set) ---
PRINT "--- Len (after set) ---"
PRINT "Len: "; m.Len

' --- IsEmpty (after set) ---
PRINT "--- IsEmpty (after set) ---"
PRINT "IsEmpty: "; m.IsEmpty

' --- Get ---
PRINT "--- Get ---"
DIM nameVal AS OBJECT = m.Get("name")
PRINT "Get(name): "; Viper.Core.Box.ToStr(nameVal)
DIM ageVal AS OBJECT = m.Get("age")
PRINT "Get(age): "; Viper.Core.Box.ToI64(ageVal)

' --- Get (missing key) ---
PRINT "--- Get (missing key) ---"
DIM missingVal AS OBJECT = m.Get("phone")
PRINT "Get(phone) returned (expect empty for null)"

' --- GetOr ---
PRINT "--- GetOr ---"
DIM cityVal AS OBJECT = m.GetOr("city", Viper.Core.Box.Str("Unknown"))
PRINT "GetOr(city, Unknown): "; Viper.Core.Box.ToStr(cityVal)
DIM phoneVal AS OBJECT = m.GetOr("phone", Viper.Core.Box.Str("N/A"))
PRINT "GetOr(phone, N/A): "; Viper.Core.Box.ToStr(phoneVal)

' --- Has ---
PRINT "--- Has ---"
PRINT "Has(name): "; m.Has("name")
PRINT "Has(phone): "; m.Has("phone")

' --- SetIfMissing ---
PRINT "--- SetIfMissing ---"
DIM ins1 AS INTEGER = m.SetIfMissing("name", Viper.Core.Box.Str("Bob"))
PRINT "SetIfMissing(name, Bob): "; ins1
PRINT "Get(name) still: "; Viper.Core.Box.ToStr(m.Get("name"))

DIM ins2 AS INTEGER = m.SetIfMissing("email", Viper.Core.Box.Str("alice@test.com"))
PRINT "SetIfMissing(email, ...): "; ins2
PRINT "Get(email): "; Viper.Core.Box.ToStr(m.Get("email"))

' --- Keys ---
PRINT "--- Keys ---"
DIM keys AS Viper.Collections.Seq = m.Keys()
PRINT "Keys count: "; keys.Len

' --- Values ---
PRINT "--- Values ---"
DIM vals AS Viper.Collections.Seq = m.Values()
PRINT "Values count: "; vals.Len

' --- Remove ---
PRINT "--- Remove ---"
DIM removed AS INTEGER = m.Remove("email")
PRINT "Remove(email): "; removed
PRINT "Has(email) after remove: "; m.Has("email")
DIM removeMissing AS INTEGER = m.Remove("nonexistent")
PRINT "Remove(nonexistent): "; removeMissing
PRINT "Len after remove: "; m.Len

' --- Clear ---
PRINT "--- Clear ---"
m.Clear()
PRINT "Len after Clear: "; m.Len
PRINT "IsEmpty after Clear: "; m.IsEmpty

PRINT "=== ConcurrentMap Audit Complete ==="
END
