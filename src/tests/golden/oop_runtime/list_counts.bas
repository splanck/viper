DIM l AS Viper.System.Collections.List
l = NEW Viper.System.Collections.List()

' Add an object; we use the list itself as a placeholder object reference.
l.Add(l)
PRINT l.Count

l.Clear()
PRINT l.Count

END

