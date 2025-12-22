' Edge case testing for Fmt operations

DIM result AS STRING

PRINT "=== Fmt Edge Case Tests ==="

' === Integer formatting ===
PRINT "=== Integer Formatting ==="

result = Viper.Fmt.Int(0)
PRINT "Int(0): '"; result; "'"

result = Viper.Fmt.Int(-1)
PRINT "Int(-1): '"; result; "'"

result = Viper.Fmt.Int(9223372036854775807)
PRINT "Int(MAX_INT64): '"; result; "'"

result = Viper.Fmt.Int(-9223372036854775807)
PRINT "Int(MIN_INT64+1): '"; result; "'"
PRINT ""

' === Num formatting (floats) ===
PRINT "=== Num Formatting ==="

result = Viper.Fmt.Num(0.0)
PRINT "Num(0.0): '"; result; "'"

result = Viper.Fmt.Num(-0.0)
PRINT "Num(-0.0): '"; result; "'"

result = Viper.Fmt.Num(1.0)
PRINT "Num(1.0): '"; result; "'"

result = Viper.Fmt.Num(0.1)
PRINT "Num(0.1): '"; result; "'"

result = Viper.Fmt.Num(0.123456789)
PRINT "Num(0.123456789): '"; result; "'"

result = Viper.Fmt.Num(1234567890.123)
PRINT "Num(1234567890.123): '"; result; "'"

result = Viper.Fmt.Num(0.0000001)
PRINT "Num(0.0000001): '"; result; "'"

result = Viper.Fmt.Num(1e10)
PRINT "Num(1e10): '"; result; "'"

result = Viper.Fmt.Num(1e-10)
PRINT "Num(1e-10): '"; result; "'"
PRINT ""

' === NumFixed formatting ===
PRINT "=== NumFixed Formatting ==="

result = Viper.Fmt.NumFixed(1.23456789, 0)
PRINT "NumFixed(1.23456789, 0): '"; result; "'"

result = Viper.Fmt.NumFixed(1.23456789, 2)
PRINT "NumFixed(1.23456789, 2): '"; result; "'"

result = Viper.Fmt.NumFixed(1.23456789, 10)
PRINT "NumFixed(1.23456789, 10): '"; result; "'"

result = Viper.Fmt.NumFixed(1.23456789, -1)
PRINT "NumFixed(1.23456789, -1): '"; result; "'"
PRINT ""

' === Size formatting ===
PRINT "=== Size Formatting ==="

result = Viper.Fmt.Size(0)
PRINT "Size(0): '"; result; "'"

result = Viper.Fmt.Size(1)
PRINT "Size(1): '"; result; "'"

result = Viper.Fmt.Size(1023)
PRINT "Size(1023): '"; result; "'"

result = Viper.Fmt.Size(1024)
PRINT "Size(1024): '"; result; "'"

result = Viper.Fmt.Size(1025)
PRINT "Size(1025): '"; result; "'"

result = Viper.Fmt.Size(1048576)
PRINT "Size(1048576 = 1MB): '"; result; "'"

result = Viper.Fmt.Size(1073741824)
PRINT "Size(1073741824 = 1GB): '"; result; "'"

result = Viper.Fmt.Size(1099511627776)
PRINT "Size(1TB): '"; result; "'"

result = Viper.Fmt.Size(-1)
PRINT "Size(-1): '"; result; "'"
PRINT ""

' === Hex formatting ===
PRINT "=== Hex Formatting ==="

result = Viper.Fmt.Hex(0)
PRINT "Hex(0): '"; result; "'"

result = Viper.Fmt.Hex(255)
PRINT "Hex(255): '"; result; "'"

result = Viper.Fmt.Hex(65535)
PRINT "Hex(65535): '"; result; "'"

result = Viper.Fmt.Hex(-1)
PRINT "Hex(-1): '"; result; "'"

result = Viper.Fmt.HexPad(255, 4)
PRINT "HexPad(255, 4): '"; result; "'"

result = Viper.Fmt.HexPad(255, 8)
PRINT "HexPad(255, 8): '"; result; "'"
PRINT ""

' === Binary formatting ===
PRINT "=== Binary Formatting ==="

result = Viper.Fmt.Bin(0)
PRINT "Bin(0): '"; result; "'"

result = Viper.Fmt.Bin(255)
PRINT "Bin(255): '"; result; "'"

result = Viper.Fmt.Bin(-1)
PRINT "Bin(-1): '"; result; "'"
PRINT ""

' === Octal formatting ===
PRINT "=== Octal Formatting ==="

result = Viper.Fmt.Oct(0)
PRINT "Oct(0): '"; result; "'"

result = Viper.Fmt.Oct(64)
PRINT "Oct(64): '"; result; "'"

result = Viper.Fmt.Oct(511)
PRINT "Oct(511): '"; result; "'"
PRINT ""

' === IntRadix formatting ===
PRINT "=== IntRadix Formatting ==="

result = Viper.Fmt.IntRadix(255, 2)
PRINT "IntRadix(255, 2): '"; result; "'"

result = Viper.Fmt.IntRadix(255, 16)
PRINT "IntRadix(255, 16): '"; result; "'"

result = Viper.Fmt.IntRadix(100, 10)
PRINT "IntRadix(100, 10): '"; result; "'"

result = Viper.Fmt.IntRadix(35, 36)
PRINT "IntRadix(35, 36): '"; result; "'"

' Edge case: radix out of range
result = Viper.Fmt.IntRadix(10, 1)
PRINT "IntRadix(10, 1) [invalid radix]: '"; result; "'"

result = Viper.Fmt.IntRadix(10, 37)
PRINT "IntRadix(10, 37) [invalid radix]: '"; result; "'"
PRINT ""

' === NumPct (percentage) formatting ===
PRINT "=== NumPct Formatting ==="

result = Viper.Fmt.NumPct(0.0, 1)
PRINT "NumPct(0.0, 1): '"; result; "'"

result = Viper.Fmt.NumPct(0.5, 1)
PRINT "NumPct(0.5, 1): '"; result; "'"

result = Viper.Fmt.NumPct(1.0, 1)
PRINT "NumPct(1.0, 1): '"; result; "'"

result = Viper.Fmt.NumPct(0.123456, 2)
PRINT "NumPct(0.123456, 2): '"; result; "'"

result = Viper.Fmt.NumPct(-0.5, 1)
PRINT "NumPct(-0.5, 1): '"; result; "'"
PRINT ""

PRINT "=== Fmt Edge Case Tests Complete ==="
END
