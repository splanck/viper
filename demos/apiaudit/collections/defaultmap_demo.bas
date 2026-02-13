' defaultmap_demo.bas
PRINT "=== Viper.Collections.DefaultMap Demo ==="
DIM d AS OBJECT
d = NEW Viper.Collections.DefaultMap("N/A")
d.Set("name", "viper")
PRINT d.Len
PRINT d.Has("name")
PRINT d.Has("age")
d.Remove("name")
PRINT d.Len
d.Clear()
PRINT d.Len
PRINT "done"
END
