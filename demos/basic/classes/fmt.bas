' Viper.Fmt Demo - Value Formatting Utilities
' This demo showcases all Viper.Fmt methods for formatting values

' Integer formatting
PRINT "=== Integer Formatting ==="
PRINT "Fmt.Int(12345): "; Viper.Fmt.Int(12345)
PRINT "Fmt.IntRadix(255, 16): "; Viper.Fmt.IntRadix(255, 16)
PRINT "Fmt.IntPad(42, 5, ""0""): "; Viper.Fmt.IntPad(42, 5, "0")
PRINT "Fmt.Hex(255): "; Viper.Fmt.Hex(255)
PRINT "Fmt.HexPad(255, 4): "; Viper.Fmt.HexPad(255, 4)
PRINT "Fmt.Bin(10): "; Viper.Fmt.Bin(10)
PRINT "Fmt.Oct(64): "; Viper.Fmt.Oct(64)
PRINT

' Floating-point formatting
PRINT "=== Floating-Point Formatting ==="
PRINT "Fmt.Num(3.14159): "; Viper.Fmt.Num(3.14159)
PRINT "Fmt.NumFixed(3.14159, 2): "; Viper.Fmt.NumFixed(3.14159, 2)
PRINT "Fmt.NumSci(1234.5, 2): "; Viper.Fmt.NumSci(1234.5, 2)
PRINT "Fmt.NumPct(0.756, 1): "; Viper.Fmt.NumPct(0.756, 1)
PRINT

' Boolean formatting
PRINT "=== Boolean Formatting ==="
PRINT "Fmt.Bool(TRUE): "; Viper.Fmt.Bool(TRUE)
PRINT "Fmt.Bool(FALSE): "; Viper.Fmt.Bool(FALSE)
PRINT "Fmt.BoolYN(TRUE): "; Viper.Fmt.BoolYN(TRUE)
PRINT "Fmt.BoolYN(FALSE): "; Viper.Fmt.BoolYN(FALSE)
PRINT

' Size formatting
PRINT "=== Size Formatting ==="
PRINT "Fmt.Size(500): "; Viper.Fmt.Size(500)
PRINT "Fmt.Size(1024): "; Viper.Fmt.Size(1024)
PRINT "Fmt.Size(1048576): "; Viper.Fmt.Size(1048576)
PRINT "Fmt.Size(1073741824): "; Viper.Fmt.Size(1073741824)

END
