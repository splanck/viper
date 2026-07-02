' =============================================================================
' API Audit: Viper.Core.Convert + Viper.Core.Parse
' =============================================================================
' Tests Convert: ToDouble, ToInt, ToInt64, ToStringInt, ToStringDouble, NumToInt
' Tests Parse: IntOr, DoubleOr, BoolOr, IsInt, IsNum, IntRadix
' =============================================================================

PRINT "=== API Audit: Viper.Core.Convert + Parse ==="

' --- Convert.ToDouble ---
PRINT "--- Convert.ToDouble ---"
PRINT "ToDouble('3.14'): "; Viper.Core.Convert.ToDouble("3.14")
PRINT "ToDouble('0.0'): "; Viper.Core.Convert.ToDouble("0.0")
PRINT "ToDouble('-2.5'): "; Viper.Core.Convert.ToDouble("-2.5")

' --- Convert.ToInt ---
PRINT "--- Convert.ToInt ---"
PRINT "ToInt('42'): "; Viper.Core.Convert.ToInt64("42")
PRINT "ToInt('0'): "; Viper.Core.Convert.ToInt64("0")
PRINT "ToInt('-100'): "; Viper.Core.Convert.ToInt64("-100")

' --- Convert.ToInt64 ---
PRINT "--- Convert.ToInt64 ---"
PRINT "ToInt64('1000000'): "; Viper.Core.Convert.ToInt64("1000000")
PRINT "ToInt64('-999'): "; Viper.Core.Convert.ToInt64("-999")

' --- Convert.ToStringInt ---
PRINT "--- Convert.ToStringInt ---"
PRINT "ToStringInt(42): "; Viper.Core.Convert.ToStringInt(42)
PRINT "ToStringInt(0): "; Viper.Core.Convert.ToStringInt(0)
PRINT "ToStringInt(-7): "; Viper.Core.Convert.ToStringInt(-7)

' --- Convert.ToStringDouble ---
PRINT "--- Convert.ToStringDouble ---"
PRINT "ToStringDouble(3.14): "; Viper.Core.Convert.ToStringDouble(3.14)
PRINT "ToStringDouble(0.0): "; Viper.Core.Convert.ToStringDouble(0.0)
PRINT "ToStringDouble(-1.5): "; Viper.Core.Convert.ToStringDouble(-1.5)

' --- Convert.NumToInt ---
PRINT "--- Convert.NumToInt ---"
PRINT "NumToInt(3.9): "; Viper.Core.Convert.NumToInt(3.9)
PRINT "NumToInt(-2.7): "; Viper.Core.Convert.NumToInt(-2.7)
PRINT "NumToInt(0.0): "; Viper.Core.Convert.NumToInt(0.0)

' =========================================
' Viper.Core.Parse
' =========================================

' --- Parse.IntOr ---
PRINT "--- Parse.IntOr ---"
PRINT "IntOr('123', -1): "; Viper.Core.Parse.IntOr("123", -1)
PRINT "IntOr('abc', -1): "; Viper.Core.Parse.IntOr("abc", -1)
PRINT "IntOr('', 0): "; Viper.Core.Parse.IntOr("", 0)

' --- Parse.DoubleOr ---
PRINT "--- Parse.DoubleOr ---"
PRINT "DoubleOr('3.14', 0.0): "; Viper.Core.Parse.DoubleOr("3.14", 0.0)
PRINT "DoubleOr('xyz', 99.9): "; Viper.Core.Parse.DoubleOr("xyz", 99.9)

' --- Parse.BoolOr ---
PRINT "--- Parse.BoolOr ---"
PRINT "BoolOr('true', FALSE): "; Viper.Core.Parse.BoolOr("true", FALSE)
PRINT "BoolOr('false', TRUE): "; Viper.Core.Parse.BoolOr("false", TRUE)
PRINT "BoolOr('nope', FALSE): "; Viper.Core.Parse.BoolOr("nope", FALSE)

' --- Parse.IsInt ---
PRINT "--- Parse.IsInt ---"
PRINT "IsInt('42'): "; Viper.Core.Parse.IsInt("42")
PRINT "IsInt('hello'): "; Viper.Core.Parse.IsInt("hello")
PRINT "IsInt('-7'): "; Viper.Core.Parse.IsInt("-7")
PRINT "IsInt(''): "; Viper.Core.Parse.IsInt("")

' --- Parse.IsNum ---
PRINT "--- Parse.IsNum ---"
PRINT "IsNum('3.14'): "; Viper.Core.Parse.IsNum("3.14")
PRINT "IsNum('42'): "; Viper.Core.Parse.IsNum("42")
PRINT "IsNum('abc'): "; Viper.Core.Parse.IsNum("abc")

' --- Parse.IntRadix ---
PRINT "--- Parse.IntRadix ---"
PRINT "IntRadix('FF', 16, 0): "; Viper.Core.Parse.IntRadix("FF", 16, 0)
PRINT "IntRadix('1010', 2, 0): "; Viper.Core.Parse.IntRadix("1010", 2, 0)
PRINT "IntRadix('77', 8, 0): "; Viper.Core.Parse.IntRadix("77", 8, 0)
PRINT "IntRadix('ZZZ', 16, -1): "; Viper.Core.Parse.IntRadix("ZZZ", 16, -1)

PRINT "=== Convert + Parse Demo Complete ==="
END
