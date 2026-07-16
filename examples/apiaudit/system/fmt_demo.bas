' =============================================================================
' API Audit: Viper.Text.Fmt - Value Formatting
' =============================================================================
' Tests: Int, IntRadix, IntPad, Num, NumFixed, Scientific, Percent, Bool, YesNo,
'        Size, Hex, HexPad, Bin, Oct, IntGrouped, Currency, ToWords, Ordinal
' =============================================================================

PRINT "=== API Audit: Viper.Text.Fmt ==="

' --- Int ---
PRINT "--- Int ---"
PRINT "Int(12345): "; Viper.Text.Fmt.Int(12345)
PRINT "Int(0): "; Viper.Text.Fmt.Int(0)
PRINT "Int(-99): "; Viper.Text.Fmt.Int(-99)

' --- IntRadix ---
PRINT "--- IntRadix ---"
PRINT "IntRadix(255, 16): "; Viper.Text.Fmt.IntRadix(255, 16)
PRINT "IntRadix(10, 2): "; Viper.Text.Fmt.IntRadix(10, 2)
PRINT "IntRadix(8, 8): "; Viper.Text.Fmt.IntRadix(8, 8)

' --- IntPad ---
PRINT "--- IntPad ---"
PRINT "IntPad(42, 5, '0'): "; Viper.Text.Fmt.IntPad(42, 5, "0")
PRINT "IntPad(7, 3, ' '): "; Viper.Text.Fmt.IntPad(7, 3, " ")

' --- Num ---
PRINT "--- Num ---"
PRINT "Num(3.14159): "; Viper.Text.Fmt.Num(3.14159)
PRINT "Num(0.0): "; Viper.Text.Fmt.Num(0.0)

' --- NumFixed ---
PRINT "--- NumFixed ---"
PRINT "NumFixed(3.14159, 2): "; Viper.Text.Fmt.NumFixed(3.14159, 2)
PRINT "NumFixed(3.14159, 4): "; Viper.Text.Fmt.NumFixed(3.14159, 4)
PRINT "NumFixed(100.0, 0): "; Viper.Text.Fmt.NumFixed(100.0, 0)

' --- Scientific ---
PRINT "--- Scientific ---"
PRINT "Scientific(1234.5, 2): "; Viper.Text.Fmt.Scientific(1234.5, 2)
PRINT "Scientific(0.001, 3): "; Viper.Text.Fmt.Scientific(0.001, 3)

' --- Percent ---
PRINT "--- Percent ---"
PRINT "Percent(0.756, 1): "; Viper.Text.Fmt.Percent(0.756, 1)
PRINT "Percent(1.0, 0): "; Viper.Text.Fmt.Percent(1.0, 0)
PRINT "Percent(0.5, 2): "; Viper.Text.Fmt.Percent(0.5, 2)

' --- Bool ---
PRINT "--- Bool ---"
PRINT "Bool(TRUE): "; Viper.Text.Fmt.Bool(TRUE)
PRINT "Bool(FALSE): "; Viper.Text.Fmt.Bool(FALSE)

' --- YesNo ---
PRINT "--- YesNo ---"
PRINT "YesNo(TRUE): "; Viper.Text.Fmt.YesNo(TRUE)
PRINT "YesNo(FALSE): "; Viper.Text.Fmt.YesNo(FALSE)

' --- Size ---
PRINT "--- Size ---"
PRINT "Size(500): "; Viper.Text.Fmt.SizeBytes(500)
PRINT "Size(1024): "; Viper.Text.Fmt.SizeBytes(1024)
PRINT "Size(1048576): "; Viper.Text.Fmt.SizeBytes(1048576)
PRINT "Size(1073741824): "; Viper.Text.Fmt.SizeBytes(1073741824)

' --- Hex ---
PRINT "--- Hex ---"
PRINT "Hex(255): "; Viper.Text.Fmt.Hex(255)
PRINT "Hex(0): "; Viper.Text.Fmt.Hex(0)
PRINT "Hex(4096): "; Viper.Text.Fmt.Hex(4096)

' --- HexPad ---
PRINT "--- HexPad ---"
PRINT "HexPad(255, 4): "; Viper.Text.Fmt.HexPad(255, 4)
PRINT "HexPad(15, 2): "; Viper.Text.Fmt.HexPad(15, 2)

' --- Bin ---
PRINT "--- Bin ---"
PRINT "Bin(10): "; Viper.Text.Fmt.Bin(10)
PRINT "Bin(255): "; Viper.Text.Fmt.Bin(255)
PRINT "Bin(0): "; Viper.Text.Fmt.Bin(0)

' --- Oct ---
PRINT "--- Oct ---"
PRINT "Oct(64): "; Viper.Text.Fmt.Oct(64)
PRINT "Oct(8): "; Viper.Text.Fmt.Oct(8)
PRINT "Oct(0): "; Viper.Text.Fmt.Oct(0)

' --- IntGrouped ---
PRINT "--- IntGrouped ---"
PRINT "IntGrouped(1234567, ','): "; Viper.Text.Fmt.IntGrouped(1234567, ",")
PRINT "IntGrouped(1000, '.'): "; Viper.Text.Fmt.IntGrouped(1000, ".")
PRINT "IntGrouped(42, ','): "; Viper.Text.Fmt.IntGrouped(42, ",")

' --- Currency ---
PRINT "--- Currency ---"
PRINT "Currency(1234.56, 2, '$'): "; Viper.Text.Fmt.Currency(1234.56, 2, "$")
PRINT "Currency(99.9, 2, 'EUR '): "; Viper.Text.Fmt.Currency(99.9, 2, "EUR ")

' --- ToWords ---
PRINT "--- ToWords ---"
PRINT "ToWords(0): "; Viper.Text.Fmt.ToWords(0)
PRINT "ToWords(1): "; Viper.Text.Fmt.ToWords(1)
PRINT "ToWords(42): "; Viper.Text.Fmt.ToWords(42)
PRINT "ToWords(100): "; Viper.Text.Fmt.ToWords(100)
PRINT "ToWords(1000): "; Viper.Text.Fmt.ToWords(1000)

' --- Ordinal ---
PRINT "--- Ordinal ---"
PRINT "Ordinal(1): "; Viper.Text.Fmt.Ordinal(1)
PRINT "Ordinal(2): "; Viper.Text.Fmt.Ordinal(2)
PRINT "Ordinal(3): "; Viper.Text.Fmt.Ordinal(3)
PRINT "Ordinal(4): "; Viper.Text.Fmt.Ordinal(4)
PRINT "Ordinal(11): "; Viper.Text.Fmt.Ordinal(11)
PRINT "Ordinal(21): "; Viper.Text.Fmt.Ordinal(21)

PRINT "=== Fmt Demo Complete ==="
END
