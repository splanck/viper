' =============================================================================
' API Audit: Viper.Fmt - Value Formatting
' =============================================================================
' Tests: Int, IntRadix, IntPad, Num, NumFixed, NumSci, NumPct, Bool, BoolYN,
'        Size, Hex, HexPad, Bin, Oct, IntGrouped, Currency, ToWords, Ordinal
' =============================================================================

PRINT "=== API Audit: Viper.Fmt ==="

' --- Int ---
PRINT "--- Int ---"
PRINT "Int(12345): "; Viper.Fmt.Int(12345)
PRINT "Int(0): "; Viper.Fmt.Int(0)
PRINT "Int(-99): "; Viper.Fmt.Int(-99)

' --- IntRadix ---
PRINT "--- IntRadix ---"
PRINT "IntRadix(255, 16): "; Viper.Fmt.IntRadix(255, 16)
PRINT "IntRadix(10, 2): "; Viper.Fmt.IntRadix(10, 2)
PRINT "IntRadix(8, 8): "; Viper.Fmt.IntRadix(8, 8)

' --- IntPad ---
PRINT "--- IntPad ---"
PRINT "IntPad(42, 5, '0'): "; Viper.Fmt.IntPad(42, 5, "0")
PRINT "IntPad(7, 3, ' '): "; Viper.Fmt.IntPad(7, 3, " ")

' --- Num ---
PRINT "--- Num ---"
PRINT "Num(3.14159): "; Viper.Fmt.Num(3.14159)
PRINT "Num(0.0): "; Viper.Fmt.Num(0.0)

' --- NumFixed ---
PRINT "--- NumFixed ---"
PRINT "NumFixed(3.14159, 2): "; Viper.Fmt.NumFixed(3.14159, 2)
PRINT "NumFixed(3.14159, 4): "; Viper.Fmt.NumFixed(3.14159, 4)
PRINT "NumFixed(100.0, 0): "; Viper.Fmt.NumFixed(100.0, 0)

' --- NumSci ---
PRINT "--- NumSci ---"
PRINT "NumSci(1234.5, 2): "; Viper.Fmt.NumSci(1234.5, 2)
PRINT "NumSci(0.001, 3): "; Viper.Fmt.NumSci(0.001, 3)

' --- NumPct ---
PRINT "--- NumPct ---"
PRINT "NumPct(0.756, 1): "; Viper.Fmt.NumPct(0.756, 1)
PRINT "NumPct(1.0, 0): "; Viper.Fmt.NumPct(1.0, 0)
PRINT "NumPct(0.5, 2): "; Viper.Fmt.NumPct(0.5, 2)

' --- Bool ---
PRINT "--- Bool ---"
PRINT "Bool(TRUE): "; Viper.Fmt.Bool(TRUE)
PRINT "Bool(FALSE): "; Viper.Fmt.Bool(FALSE)

' --- BoolYN ---
PRINT "--- BoolYN ---"
PRINT "BoolYN(TRUE): "; Viper.Fmt.BoolYN(TRUE)
PRINT "BoolYN(FALSE): "; Viper.Fmt.BoolYN(FALSE)

' --- Size ---
PRINT "--- Size ---"
PRINT "Size(500): "; Viper.Fmt.Size(500)
PRINT "Size(1024): "; Viper.Fmt.Size(1024)
PRINT "Size(1048576): "; Viper.Fmt.Size(1048576)
PRINT "Size(1073741824): "; Viper.Fmt.Size(1073741824)

' --- Hex ---
PRINT "--- Hex ---"
PRINT "Hex(255): "; Viper.Fmt.Hex(255)
PRINT "Hex(0): "; Viper.Fmt.Hex(0)
PRINT "Hex(4096): "; Viper.Fmt.Hex(4096)

' --- HexPad ---
PRINT "--- HexPad ---"
PRINT "HexPad(255, 4): "; Viper.Fmt.HexPad(255, 4)
PRINT "HexPad(15, 2): "; Viper.Fmt.HexPad(15, 2)

' --- Bin ---
PRINT "--- Bin ---"
PRINT "Bin(10): "; Viper.Fmt.Bin(10)
PRINT "Bin(255): "; Viper.Fmt.Bin(255)
PRINT "Bin(0): "; Viper.Fmt.Bin(0)

' --- Oct ---
PRINT "--- Oct ---"
PRINT "Oct(64): "; Viper.Fmt.Oct(64)
PRINT "Oct(8): "; Viper.Fmt.Oct(8)
PRINT "Oct(0): "; Viper.Fmt.Oct(0)

' --- IntGrouped ---
PRINT "--- IntGrouped ---"
PRINT "IntGrouped(1234567, ','): "; Viper.Fmt.IntGrouped(1234567, ",")
PRINT "IntGrouped(1000, '.'): "; Viper.Fmt.IntGrouped(1000, ".")
PRINT "IntGrouped(42, ','): "; Viper.Fmt.IntGrouped(42, ",")

' --- Currency ---
PRINT "--- Currency ---"
PRINT "Currency(1234.56, 2, '$'): "; Viper.Fmt.Currency(1234.56, 2, "$")
PRINT "Currency(99.9, 2, 'EUR '): "; Viper.Fmt.Currency(99.9, 2, "EUR ")

' --- ToWords ---
PRINT "--- ToWords ---"
PRINT "ToWords(0): "; Viper.Fmt.ToWords(0)
PRINT "ToWords(1): "; Viper.Fmt.ToWords(1)
PRINT "ToWords(42): "; Viper.Fmt.ToWords(42)
PRINT "ToWords(100): "; Viper.Fmt.ToWords(100)
PRINT "ToWords(1000): "; Viper.Fmt.ToWords(1000)

' --- Ordinal ---
PRINT "--- Ordinal ---"
PRINT "Ordinal(1): "; Viper.Fmt.Ordinal(1)
PRINT "Ordinal(2): "; Viper.Fmt.Ordinal(2)
PRINT "Ordinal(3): "; Viper.Fmt.Ordinal(3)
PRINT "Ordinal(4): "; Viper.Fmt.Ordinal(4)
PRINT "Ordinal(11): "; Viper.Fmt.Ordinal(11)
PRINT "Ordinal(21): "; Viper.Fmt.Ordinal(21)

PRINT "=== Fmt Demo Complete ==="
END
