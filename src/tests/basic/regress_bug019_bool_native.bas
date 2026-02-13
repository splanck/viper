' BUG-019 regression: Boolean (i1) return values must be properly zero-extended
' in native codegen. Without the fix, MOVrr copies garbage upper bits from RAX.
PRINT Viper.Text.Pattern.IsMatch("abc123def", "[0-9]+")
PRINT Viper.Text.Pattern.IsMatch("nodigits", "[0-9]+")
PRINT "done"
