DIM l AS Viper.Collections.List
l = NEW Viper.Collections.List()

' Add an object; we use the list itself as a placeholder object reference.
l.Push(l)
l.Push(l)
PRINT l.Length

l.RemoveAt(0)
PRINT l.Length

l.Clear()
PRINT l.Length

END
