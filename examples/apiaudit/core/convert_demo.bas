' =============================================================================
' API Audit: Zanna.Core.Convert + Zanna.Core.Parse
' =============================================================================
' Tests Convert: ToDouble, ToInt, ToInt64, ToStringInt, ToStringDouble, NumToInt
' Tests Parse: IntOr, DoubleOr, BoolOr, IsInt, IsNum, IntRadix
' =============================================================================

PRINT "=== API Audit: Zanna.Core.Convert + Parse ==="

' --- Convert.ToDouble ---
PRINT "--- Convert.ToDouble ---"
PRINT "ToDouble('3.14'): "; Zanna.Core.Convert.ToDouble("3.14")
PRINT "ToDouble('0.0'): "; Zanna.Core.Convert.ToDouble("0.0")
PRINT "ToDouble('-2.5'): "; Zanna.Core.Convert.ToDouble("-2.5")

' --- Convert.ToInt ---
PRINT "--- Convert.ToInt ---"
PRINT "ToInt('42'): "; Zanna.Core.Convert.ToInt64("42")
PRINT "ToInt('0'): "; Zanna.Core.Convert.ToInt64("0")
PRINT "ToInt('-100'): "; Zanna.Core.Convert.ToInt64("-100")

' --- Convert.ToInt64 ---
PRINT "--- Convert.ToInt64 ---"
PRINT "ToInt64('1000000'): "; Zanna.Core.Convert.ToInt64("1000000")
PRINT "ToInt64('-999'): "; Zanna.Core.Convert.ToInt64("-999")

' --- Convert.ToStringInt ---
PRINT "--- Convert.ToStringInt ---"
PRINT "ToStringInt(42): "; Zanna.Core.Convert.ToStringInt(42)
PRINT "ToStringInt(0): "; Zanna.Core.Convert.ToStringInt(0)
PRINT "ToStringInt(-7): "; Zanna.Core.Convert.ToStringInt(-7)

' --- Convert.ToStringDouble ---
PRINT "--- Convert.ToStringDouble ---"
PRINT "ToStringDouble(3.14): "; Zanna.Core.Convert.ToStringDouble(3.14)
PRINT "ToStringDouble(0.0): "; Zanna.Core.Convert.ToStringDouble(0.0)
PRINT "ToStringDouble(-1.5): "; Zanna.Core.Convert.ToStringDouble(-1.5)

' --- Convert.NumToInt ---
PRINT "--- Convert.NumToInt ---"
PRINT "NumToInt(3.9): "; Zanna.Core.Convert.NumToInt(3.9)
PRINT "NumToInt(-2.7): "; Zanna.Core.Convert.NumToInt(-2.7)
PRINT "NumToInt(0.0): "; Zanna.Core.Convert.NumToInt(0.0)

' =========================================
' Zanna.Core.Parse
' =========================================

' --- Parse.IntOr ---
PRINT "--- Parse.IntOr ---"
PRINT "IntOr('123', -1): "; Zanna.Core.Parse.IntOr("123", -1)
PRINT "IntOr('abc', -1): "; Zanna.Core.Parse.IntOr("abc", -1)
PRINT "IntOr('', 0): "; Zanna.Core.Parse.IntOr("", 0)

' --- Parse.DoubleOr ---
PRINT "--- Parse.DoubleOr ---"
PRINT "DoubleOr('3.14', 0.0): "; Zanna.Core.Parse.DoubleOr("3.14", 0.0)
PRINT "DoubleOr('xyz', 99.9): "; Zanna.Core.Parse.DoubleOr("xyz", 99.9)

' --- Parse.BoolOr ---
PRINT "--- Parse.BoolOr ---"
PRINT "BoolOr('true', FALSE): "; Zanna.Core.Parse.BoolOr("true", FALSE)
PRINT "BoolOr('false', TRUE): "; Zanna.Core.Parse.BoolOr("false", TRUE)
PRINT "BoolOr('nope', FALSE): "; Zanna.Core.Parse.BoolOr("nope", FALSE)

' --- Parse.IsInt ---
PRINT "--- Parse.IsInt ---"
PRINT "IsInt('42'): "; Zanna.Core.Parse.IsInt("42")
PRINT "IsInt('hello'): "; Zanna.Core.Parse.IsInt("hello")
PRINT "IsInt('-7'): "; Zanna.Core.Parse.IsInt("-7")
PRINT "IsInt(''): "; Zanna.Core.Parse.IsInt("")

' --- Parse.IsNum ---
PRINT "--- Parse.IsNum ---"
PRINT "IsNum('3.14'): "; Zanna.Core.Parse.IsNum("3.14")
PRINT "IsNum('42'): "; Zanna.Core.Parse.IsNum("42")
PRINT "IsNum('abc'): "; Zanna.Core.Parse.IsNum("abc")

' --- Parse.IntRadix ---
PRINT "--- Parse.IntRadix ---"
PRINT "IntRadix('FF', 16, 0): "; Zanna.Core.Parse.IntRadix("FF", 16, 0)
PRINT "IntRadix('1010', 2, 0): "; Zanna.Core.Parse.IntRadix("1010", 2, 0)
PRINT "IntRadix('77', 8, 0): "; Zanna.Core.Parse.IntRadix("77", 8, 0)
PRINT "IntRadix('ZZZ', 16, -1): "; Zanna.Core.Parse.IntRadix("ZZZ", 16, -1)

PRINT "=== Convert + Parse Demo Complete ==="
END
