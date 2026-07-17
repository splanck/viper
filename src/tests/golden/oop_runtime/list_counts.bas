DIM l AS Zanna.Collections.List
l = NEW Zanna.Collections.List()

' Add an object; we use the list itself as a placeholder object reference.
l.Push(l)
l.Push(l)
PRINT l.Count

l.RemoveAt(0)
PRINT l.Count

l.Clear()
PRINT l.Count

END
