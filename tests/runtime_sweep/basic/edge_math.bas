' Edge case testing for Math operations
' Looking for crashes, incorrect results, or unexpected behavior

DIM result AS DOUBLE
DIM iresult AS INTEGER

' === Sqrt edge cases ===
PRINT "=== Sqrt Edge Cases ==="

result = Viper.Math.Sqrt(0)
PRINT "Sqrt(0): "; result

result = Viper.Math.Sqrt(-1)
PRINT "Sqrt(-1): "; result

result = Viper.Math.Sqrt(1e308)
PRINT "Sqrt(1e308): "; result

PRINT ""

' === Log edge cases ===
PRINT "=== Log Edge Cases ==="

result = Viper.Math.Log(0)
PRINT "Log(0): "; result

result = Viper.Math.Log(-1)
PRINT "Log(-1): "; result

result = Viper.Math.Log(1)
PRINT "Log(1): "; result

PRINT ""

' === Pow edge cases ===
PRINT "=== Pow Edge Cases ==="

result = Viper.Math.Pow(0, 0)
PRINT "Pow(0, 0): "; result

result = Viper.Math.Pow(0, 1)
PRINT "Pow(0, 1): "; result

' These cause traps:
PRINT "Pow(0, -1): [skipped - causes overflow trap]"
PRINT "Pow(-1, 0.5): [skipped - causes DomainError trap]"

result = Viper.Math.Pow(2, 1000)
PRINT "Pow(2, 1000): "; result

result = Viper.Math.Pow(2, -1000)
PRINT "Pow(2, -1000): "; result

PRINT ""

' === Trig edge cases ===
PRINT "=== Trig Edge Cases ==="

result = Viper.Math.Sin(1e308)
PRINT "Sin(1e308): "; result

result = Viper.Math.Asin(2)
PRINT "Asin(2): "; result

result = Viper.Math.Acos(2)
PRINT "Acos(2): "; result

result = Viper.Math.Tan(Viper.Math.Pi / 2)
PRINT "Tan(Pi/2): "; result

PRINT ""

' === Min/Max edge cases ===
PRINT "=== Min/Max Edge Cases ==="

result = Viper.Math.Min(1e308, -1e308)
PRINT "Min(1e308, -1e308): "; result

result = Viper.Math.Max(1e308, -1e308)
PRINT "Max(1e308, -1e308): "; result

PRINT ""

' === Abs edge cases ===
PRINT "=== Abs Edge Cases ==="

result = Viper.Math.Abs(-1e308)
PRINT "Abs(-1e308): "; result

PRINT ""

' === Floor/Ceil/Round edge cases ===
PRINT "=== Floor/Ceil/Round Edge Cases ==="

result = Viper.Math.Floor(-0.5)
PRINT "Floor(-0.5): "; result

result = Viper.Math.Ceil(-0.5)
PRINT "Ceil(-0.5): "; result

result = Viper.Math.Round(-0.5)
PRINT "Round(-0.5): "; result

result = Viper.Math.Round(0.5)
PRINT "Round(0.5): "; result

result = Viper.Math.Round(1.5)
PRINT "Round(1.5): "; result

result = Viper.Math.Round(2.5)
PRINT "Round(2.5): "; result

PRINT ""

' === Clamp edge cases ===
PRINT "=== Clamp Edge Cases ==="

result = Viper.Math.Clamp(5, 10, 1)
PRINT "Clamp(5, 10, 1) [min > max]: "; result

result = Viper.Math.Clamp(5, 5, 5)
PRINT "Clamp(5, 5, 5) [all same]: "; result

PRINT ""

' === Bits edge cases ===
PRINT "=== Bits Edge Cases ==="

iresult = Viper.Math.Bits.Shl(1, 63)
PRINT "Shl(1, 63): "; iresult

iresult = Viper.Math.Bits.Shl(1, 64)
PRINT "Shl(1, 64): "; iresult

iresult = Viper.Math.Bits.Shl(1, -1)
PRINT "Shl(1, -1): "; iresult

iresult = Viper.Math.Bits.Shr(-1, 1)
PRINT "Shr(-1, 1): "; iresult

iresult = Viper.Math.Bits.Count(0)
PRINT "Count(0): "; iresult

iresult = Viper.Math.Bits.Count(-1)
PRINT "Count(-1): "; iresult

PRINT ""
PRINT "=== Math Edge Case Tests Complete ==="
END
