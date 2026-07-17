' BUG-001 regression: PRINT should handle ptr/object return types
' Previously caused IL type mismatch when printing an object value
DIM m AS Zanna.Collections.Map = NEW Zanna.Collections.Map()
m.Set("key", "value")
PRINT m
