' =============================================================================
' API Audit: Viper.Core.Box - Value Boxing/Unboxing
' =============================================================================
' Tests: I64, F64, I1, Str, ToI64, ToF64, ToI1, ToStr, Type, EqI64, EqF64,
'        EqStr, ValueType
' =============================================================================

PRINT "=== API Audit: Viper.Core.Box ==="

' --- Box I64 ---
PRINT "--- Box I64 ---"
DIM bi AS OBJECT
bi = Viper.Core.Box.I64(42)
PRINT "Box.I64(42) created"
PRINT "ToI64: "; Viper.Core.Box.ToI64(bi)
PRINT "Type (expect 0 for i64): "; Viper.Core.Box.Type(bi)
PRINT "EqI64(42): "; Viper.Core.Box.EqI64(bi, 42)
PRINT "EqI64(99): "; Viper.Core.Box.EqI64(bi, 99)

' --- Box F64 ---
PRINT "--- Box F64 ---"
DIM bf AS OBJECT
bf = Viper.Core.Box.F64(3.14)
PRINT "Box.F64(3.14) created"
PRINT "ToF64: "; Viper.Core.Box.ToF64(bf)
PRINT "Type (expect 1 for f64): "; Viper.Core.Box.Type(bf)
PRINT "EqF64(3.14): "; Viper.Core.Box.EqF64(bf, 3.14)
PRINT "EqF64(2.71): "; Viper.Core.Box.EqF64(bf, 2.71)

' --- Box I1 (Boolean) ---
PRINT "--- Box I1 ---"
DIM bb AS OBJECT
bb = Viper.Core.Box.I1(1)
PRINT "Box.I1(1) created"
PRINT "ToI1: "; Viper.Core.Box.ToI1(bb)
PRINT "Type (expect 2 for i1): "; Viper.Core.Box.Type(bb)

DIM bb0 AS OBJECT
bb0 = Viper.Core.Box.I1(0)
PRINT "Box.I1(0) ToI1: "; Viper.Core.Box.ToI1(bb0)

' --- Box Str ---
PRINT "--- Box Str ---"
DIM bs AS OBJECT
bs = Viper.Core.Box.Str("hello")
PRINT "Box.Str(hello) created"
PRINT "ToStr: "; Viper.Core.Box.ToStr(bs)
PRINT "Type (expect 3 for str): "; Viper.Core.Box.Type(bs)
PRINT "EqStr(hello): "; Viper.Core.Box.EqStr(bs, "hello")
PRINT "EqStr(world): "; Viper.Core.Box.EqStr(bs, "world")

' --- ValueType ---
PRINT "--- ValueType ---"
DIM vt0 AS OBJECT
vt0 = Viper.Core.Box.ValueType(0)
PRINT "ValueType(0) - i64 type obj created"
DIM vt1 AS OBJECT
vt1 = Viper.Core.Box.ValueType(1)
PRINT "ValueType(1) - f64 type obj created"
DIM vt2 AS OBJECT
vt2 = Viper.Core.Box.ValueType(2)
PRINT "ValueType(2) - i1 type obj created"
DIM vt3 AS OBJECT
vt3 = Viper.Core.Box.ValueType(3)
PRINT "ValueType(3) - str type obj created"

PRINT "=== Box Demo Complete ==="
END
