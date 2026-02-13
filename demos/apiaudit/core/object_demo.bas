' =============================================================================
' API Audit: Viper.Core.Object - Base Object Operations
' =============================================================================
' Tests: Equals, HashCode, RefEquals, ToString, TypeName, TypeId, IsNull
' =============================================================================

PRINT "=== API Audit: Viper.Core.Object ==="

' Create test objects via boxing
DIM a AS OBJECT
DIM b AS OBJECT
DIM c AS OBJECT
a = Viper.Core.Box.I64(42)
b = Viper.Core.Box.I64(42)
c = a

' --- Equals ---
PRINT "--- Equals ---"
PRINT "Equals(a, b) - same value, diff obj: "; Viper.Core.Object.Equals(a, b)
PRINT "Equals(a, c) - same reference: "; Viper.Core.Object.Equals(a, c)

' --- HashCode ---
PRINT "--- HashCode ---"
PRINT "HashCode(a): "; Viper.Core.Object.HashCode(a)
PRINT "HashCode(b): "; Viper.Core.Object.HashCode(b)

' --- RefEquals ---
PRINT "--- RefEquals ---"
PRINT "RefEquals(a, c) - same ref: "; Viper.Core.Object.RefEquals(a, c)
PRINT "RefEquals(a, b) - diff ref: "; Viper.Core.Object.RefEquals(a, b)

' --- ToString ---
PRINT "--- ToString ---"
DIM s AS OBJECT
s = Viper.Core.Box.Str("world")
PRINT "ToString on boxed string: "; Viper.Core.Object.ToString(s)
PRINT "ToString on boxed int: "; Viper.Core.Object.ToString(a)

' --- TypeName ---
PRINT "--- TypeName ---"
PRINT "TypeName(boxed i64): "; Viper.Core.Object.TypeName(a)
PRINT "TypeName(boxed str): "; Viper.Core.Object.TypeName(s)

' --- TypeId ---
PRINT "--- TypeId ---"
PRINT "TypeId(boxed i64): "; Viper.Core.Object.TypeId(a)
PRINT "TypeId(boxed str): "; Viper.Core.Object.TypeId(s)

' --- IsNull ---
PRINT "--- IsNull ---"
PRINT "IsNull(a) - not null: "; Viper.Core.Object.IsNull(a)

PRINT "=== Object Demo Complete ==="
END
