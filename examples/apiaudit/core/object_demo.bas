' =============================================================================
' API Audit: Zanna.Core.Object - Base Object Operations
' =============================================================================
' Tests: Equals, HashCode, RefEquals, ToString, TypeName, TypeId, IsNull
' =============================================================================

PRINT "=== API Audit: Zanna.Core.Object ==="

' Create test objects via boxing
DIM a AS OBJECT
DIM b AS OBJECT
DIM c AS OBJECT
a = Zanna.Core.Box.I64(42)
b = Zanna.Core.Box.I64(42)
c = a

' --- Equals ---
PRINT "--- Equals ---"
PRINT "Equals(a, b) - same value, diff obj: "; Zanna.Core.Object.Equals(a, b)
PRINT "Equals(a, c) - same reference: "; Zanna.Core.Object.Equals(a, c)

' --- HashCode ---
PRINT "--- HashCode ---"
PRINT "HashCode(a): "; Zanna.Core.Object.HashCode(a)
PRINT "HashCode(b): "; Zanna.Core.Object.HashCode(b)

' --- RefEquals ---
PRINT "--- RefEquals ---"
PRINT "RefEquals(a, c) - same ref: "; Zanna.Core.Object.RefEquals(a, c)
PRINT "RefEquals(a, b) - diff ref: "; Zanna.Core.Object.RefEquals(a, b)

' --- ToString ---
PRINT "--- ToString ---"
DIM s AS OBJECT
s = Zanna.Core.Box.Str("world")
PRINT "ToString on boxed string: "; Zanna.Core.Object.ToString(s)
PRINT "ToString on boxed int: "; Zanna.Core.Object.ToString(a)

' --- TypeName ---
PRINT "--- TypeName ---"
PRINT "TypeName(boxed i64): "; Zanna.Core.Object.TypeName(a)
PRINT "TypeName(boxed str): "; Zanna.Core.Object.TypeName(s)

' --- TypeId ---
PRINT "--- TypeId ---"
PRINT "TypeId(boxed i64): "; Zanna.Core.Object.TypeId(a)
PRINT "TypeId(boxed str): "; Zanna.Core.Object.TypeId(s)

' --- IsNull ---
PRINT "--- IsNull ---"
PRINT "IsNull(a) - not null: "; Zanna.Core.Object.IsNull(a)

PRINT "=== Object Demo Complete ==="
END
