' numfmt_demo.bas
PRINT "=== Viper.Text.NumberFormat Demo ==="
PRINT Viper.Text.NumberFormat.Ordinal(1)
PRINT Viper.Text.NumberFormat.Ordinal(42)
PRINT Viper.Text.NumberFormat.Bytes(1048576)
PRINT Viper.Text.NumberFormat.Percent(0.756)
PRINT Viper.Text.NumberFormat.Decimals(3.14159, 2)
PRINT Viper.Text.NumberFormat.Currency(19.99, "$")
PRINT Viper.Text.NumberFormat.Pad(7, 3)
PRINT Viper.Text.NumberFormat.Thousands(1000000, ",")
PRINT Viper.Text.NumberFormat.ToWords(42)
PRINT "done"
END
