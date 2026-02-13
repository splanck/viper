' frozenset_demo.bas
PRINT "=== Viper.Collections.FrozenSet Demo ==="
DIM s AS OBJECT
s = NEW Viper.Collections.Seq()
s.Push("apple")
s.Push("banana")
s.Push("cherry")
DIM fs AS OBJECT
fs = NEW Viper.Collections.FrozenSet(s)
PRINT fs.Len
PRINT fs.Has("apple")
PRINT fs.Has("grape")
PRINT fs.IsEmpty
PRINT "done"
END
