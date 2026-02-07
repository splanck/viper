DIM l AS Viper.Collections.List
l = NEW Viper.Collections.List()

' Add an object; we use the list itself as a placeholder object reference.
l.Push(l)
l.Push(l)
PRINT l.Len

l.RemoveAt(0)
PRINT l.Len

l.Clear()
PRINT l.Len

END
