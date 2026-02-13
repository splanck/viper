' set_demo.bas
PRINT "=== Viper.Collections.Set Demo ==="
DIM s AS OBJECT
s = NEW Viper.Collections.Set()
s.Add("a")
s.Add("b")
s.Add("c")
s.Add("a")
PRINT s.Len
PRINT s.Has("a")
PRINT s.Has("z")
s.Remove("b")
PRINT s.Len
s.Clear()
PRINT s.IsEmpty
PRINT "done"
END
