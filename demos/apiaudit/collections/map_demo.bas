' map_demo.bas
PRINT "=== Viper.Collections.Map Demo ==="
DIM m AS OBJECT
m = NEW Viper.Collections.Map()
m.Set("name", "viper")
m.Set("version", "1.0")
m.Set("lang", "zia")
PRINT m.Len
PRINT m.Has("name")
PRINT m.Has("missing")
PRINT m.Remove("lang")
PRINT m.Len
m.Clear()
PRINT m.IsEmpty
PRINT "done"
END
