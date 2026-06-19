' Viper.Text.InvariantNumberFormat API Audit
' Tests all NumberFormat functions

PRINT "=== Viper.Text.InvariantNumberFormat API Audit ==="

' --- Decimals ---
PRINT "--- Decimals ---"
PRINT Viper.Text.InvariantNumberFormat.Decimals(3.14159, 2)
PRINT Viper.Text.InvariantNumberFormat.Decimals(3.14159, 4)
PRINT Viper.Text.InvariantNumberFormat.Decimals(1.0, 3)
PRINT Viper.Text.InvariantNumberFormat.Decimals(0.5, 1)

' --- Thousands ---
PRINT "--- Thousands ---"
PRINT Viper.Text.InvariantNumberFormat.Thousands(1234567, ",")
PRINT Viper.Text.InvariantNumberFormat.Thousands(1000, ".")
PRINT Viper.Text.InvariantNumberFormat.Thousands(999, ",")
PRINT Viper.Text.InvariantNumberFormat.Thousands(0, ",")

' --- Currency ---
PRINT "--- Currency ---"
PRINT Viper.Text.InvariantNumberFormat.Currency(1234.56, "$")
PRINT Viper.Text.InvariantNumberFormat.Currency(99.9, "$")
PRINT Viper.Text.InvariantNumberFormat.Currency(0.5, "$")

' --- Percent ---
PRINT "--- Percent ---"
PRINT Viper.Text.InvariantNumberFormat.Percent(0.75)
PRINT Viper.Text.InvariantNumberFormat.Percent(1.0)
PRINT Viper.Text.InvariantNumberFormat.Percent(0.123)

' --- Ordinal ---
PRINT "--- Ordinal ---"
PRINT Viper.Text.InvariantNumberFormat.Ordinal(1)
PRINT Viper.Text.InvariantNumberFormat.Ordinal(2)
PRINT Viper.Text.InvariantNumberFormat.Ordinal(3)
PRINT Viper.Text.InvariantNumberFormat.Ordinal(4)
PRINT Viper.Text.InvariantNumberFormat.Ordinal(11)
PRINT Viper.Text.InvariantNumberFormat.Ordinal(12)
PRINT Viper.Text.InvariantNumberFormat.Ordinal(13)
PRINT Viper.Text.InvariantNumberFormat.Ordinal(21)
PRINT Viper.Text.InvariantNumberFormat.Ordinal(22)
PRINT Viper.Text.InvariantNumberFormat.Ordinal(23)
PRINT Viper.Text.InvariantNumberFormat.Ordinal(101)

' --- ToWords ---
PRINT "--- ToWords ---"
PRINT Viper.Text.InvariantNumberFormat.ToWords(0)
PRINT Viper.Text.InvariantNumberFormat.ToWords(1)
PRINT Viper.Text.InvariantNumberFormat.ToWords(42)
PRINT Viper.Text.InvariantNumberFormat.ToWords(100)
PRINT Viper.Text.InvariantNumberFormat.ToWords(1000)
PRINT Viper.Text.InvariantNumberFormat.ToWords(1234)

' --- Bytes ---
PRINT "--- Bytes ---"
PRINT Viper.Text.InvariantNumberFormat.Bytes(0)
PRINT Viper.Text.InvariantNumberFormat.Bytes(512)
PRINT Viper.Text.InvariantNumberFormat.Bytes(1024)
PRINT Viper.Text.InvariantNumberFormat.Bytes(1048576)
PRINT Viper.Text.InvariantNumberFormat.Bytes(1073741824)

' --- Pad ---
PRINT "--- Pad ---"
PRINT Viper.Text.InvariantNumberFormat.Pad(42, 6)
PRINT Viper.Text.InvariantNumberFormat.Pad(1, 4)
PRINT Viper.Text.InvariantNumberFormat.Pad(12345, 3)

PRINT "=== NumberFormat Demo Complete ==="
END
