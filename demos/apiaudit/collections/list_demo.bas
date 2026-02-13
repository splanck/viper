' list_demo.bas — using correct method names from RT_CLASS
PRINT "=== Viper.Collections.List Demo ==="
DIM l AS OBJECT
l = NEW Viper.Collections.List()
l.Push("alpha")
l.Push("beta")
l.Push("gamma")
PRINT l.Len
PRINT l.Find("gamma")
PRINT l.Has("alpha")
PRINT l.Has("xyz")
l.Insert(0, "first")
PRINT l.Len
l.RemoveAt(0)
PRINT l.Len
l.Flip()
l.Clear()
PRINT l.Len
PRINT "done"
END
