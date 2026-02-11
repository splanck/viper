' BUG-001 regression: PRINT should handle ptr/object return types
' Previously caused IL type mismatch when printing an object value
DIM m AS Viper.Collections.Map = NEW Viper.Collections.Map()
m.Set("key", "value")
PRINT m
