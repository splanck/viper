' BUG-019 regression: Boolean (i1) return values must be properly zero-extended
' in native codegen. Without the fix, MOVrr copies garbage upper bits from RAX.
PRINT Viper.Text.Pattern.IsMatch("[0-9]+", "abc123def")
PRINT Viper.Text.Pattern.IsMatch("[0-9]+", "nodigits")
PRINT "done"
