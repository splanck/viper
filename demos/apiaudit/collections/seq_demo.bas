' seq_demo.bas
PRINT "=== Viper.Collections.Seq Demo ==="
DIM s AS OBJECT
s = NEW Viper.Collections.Seq()
s.Push("a")
s.Push("b")
s.Push("c")
PRINT s.Len
PRINT s.Cap
PRINT s.IsEmpty
PRINT s.Has("b")
PRINT s.Find("c")
s.Reverse()
s.Clear()
PRINT s.IsEmpty
PRINT "done"
END
