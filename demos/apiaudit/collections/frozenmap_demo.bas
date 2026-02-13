' frozenmap_demo.bas
PRINT "=== Viper.Collections.FrozenMap Demo ==="
DIM keys AS OBJECT
keys = NEW Viper.Collections.Seq()
keys.Push("a")
keys.Push("b")
keys.Push("c")
DIM vals AS OBJECT
vals = NEW Viper.Collections.Seq()
vals.Push("alpha")
vals.Push("beta")
vals.Push("gamma")
DIM fm AS OBJECT
fm = NEW Viper.Collections.FrozenMap(keys, vals)
PRINT fm.Len
PRINT fm.Has("a")
PRINT fm.Has("z")
PRINT fm.IsEmpty
PRINT "done"
END
