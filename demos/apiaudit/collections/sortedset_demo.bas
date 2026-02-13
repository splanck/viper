' sortedset_demo.bas
PRINT "=== Viper.Collections.SortedSet Demo ==="
DIM s AS OBJECT
s = NEW Viper.Collections.SortedSet()
s.Put("c")
s.Put("a")
s.Put("b")
s.Put("a")
PRINT s.Len
PRINT s.Has("a")
s.Drop("b")
PRINT s.Len
PRINT s.First()
PRINT s.Last()
s.Clear()
PRINT s.IsEmpty
PRINT "done"
END
