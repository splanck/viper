DIM l AS Viper.Collections.List
l = NEW Viper.Collections.List()

' Add an object; we use the list itself as a placeholder object reference.
l.Add(l)
l.Add(l)
PRINT l.Count

l.RemoveAt(0)
PRINT l.Count

l.Clear()
PRINT l.Count

END
