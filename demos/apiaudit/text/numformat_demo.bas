' Viper.Text.NumberFormat API Audit
' Tests all NumberFormat functions

PRINT "=== Viper.Text.NumberFormat API Audit ==="

' --- Decimals ---
PRINT "--- Decimals ---"
PRINT Viper.Text.NumberFormat.Decimals(3.14159, 2)
PRINT Viper.Text.NumberFormat.Decimals(3.14159, 4)
PRINT Viper.Text.NumberFormat.Decimals(1.0, 3)
PRINT Viper.Text.NumberFormat.Decimals(0.5, 1)

' --- Thousands ---
PRINT "--- Thousands ---"
PRINT Viper.Text.NumberFormat.Thousands(1234567, ",")
PRINT Viper.Text.NumberFormat.Thousands(1000, ".")
PRINT Viper.Text.NumberFormat.Thousands(999, ",")
PRINT Viper.Text.NumberFormat.Thousands(0, ",")

' --- Currency ---
PRINT "--- Currency ---"
PRINT Viper.Text.NumberFormat.Currency(1234.56, "$")
PRINT Viper.Text.NumberFormat.Currency(99.9, "$")
PRINT Viper.Text.NumberFormat.Currency(0.5, "$")

' --- Percent ---
PRINT "--- Percent ---"
PRINT Viper.Text.NumberFormat.Percent(0.75)
PRINT Viper.Text.NumberFormat.Percent(1.0)
PRINT Viper.Text.NumberFormat.Percent(0.123)

' --- Ordinal ---
PRINT "--- Ordinal ---"
PRINT Viper.Text.NumberFormat.Ordinal(1)
PRINT Viper.Text.NumberFormat.Ordinal(2)
PRINT Viper.Text.NumberFormat.Ordinal(3)
PRINT Viper.Text.NumberFormat.Ordinal(4)
PRINT Viper.Text.NumberFormat.Ordinal(11)
PRINT Viper.Text.NumberFormat.Ordinal(12)
PRINT Viper.Text.NumberFormat.Ordinal(13)
PRINT Viper.Text.NumberFormat.Ordinal(21)
PRINT Viper.Text.NumberFormat.Ordinal(22)
PRINT Viper.Text.NumberFormat.Ordinal(23)
PRINT Viper.Text.NumberFormat.Ordinal(101)

' --- ToWords ---
PRINT "--- ToWords ---"
PRINT Viper.Text.NumberFormat.ToWords(0)
PRINT Viper.Text.NumberFormat.ToWords(1)
PRINT Viper.Text.NumberFormat.ToWords(42)
PRINT Viper.Text.NumberFormat.ToWords(100)
PRINT Viper.Text.NumberFormat.ToWords(1000)
PRINT Viper.Text.NumberFormat.ToWords(1234)

' --- Bytes ---
PRINT "--- Bytes ---"
PRINT Viper.Text.NumberFormat.Bytes(0)
PRINT Viper.Text.NumberFormat.Bytes(512)
PRINT Viper.Text.NumberFormat.Bytes(1024)
PRINT Viper.Text.NumberFormat.Bytes(1048576)
PRINT Viper.Text.NumberFormat.Bytes(1073741824)

' --- Pad ---
PRINT "--- Pad ---"
PRINT Viper.Text.NumberFormat.Pad(42, 6)
PRINT Viper.Text.NumberFormat.Pad(1, 4)
PRINT Viper.Text.NumberFormat.Pad(12345, 3)

PRINT "=== NumberFormat Demo Complete ==="
END
