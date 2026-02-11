' BUG-023 regression: string-to-obj coercion in runtime class constructors
' DefaultMap.New(obj) should accept a string argument
DIM dm AS Viper.Collections.DefaultMap = NEW Viper.Collections.DefaultMap("default_val")
dm.Set("key1", "actual_val")
PRINT "ok"
