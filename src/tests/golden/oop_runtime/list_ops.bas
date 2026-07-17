DIM l AS Zanna.Collections.List
l = NEW Zanna.Collections.List()

DIM a AS Zanna.Collections.List
DIM b AS Zanna.Collections.List
DIM c AS Zanna.Collections.List
DIM d AS Zanna.Collections.List
DIM e AS Zanna.Collections.List

a = NEW Zanna.Collections.List()
b = NEW Zanna.Collections.List()
c = NEW Zanna.Collections.List()
d = NEW Zanna.Collections.List()
e = NEW Zanna.Collections.List()

' Has on empty list
IF l.Has(a) THEN
  PRINT 1
ELSE
  PRINT 0
END IF

' Seed list: [a, c]
l.Push(a)
l.Push(c)

PRINT Zanna.Option.UnwrapOrI64(l.FindOption(a), -1)
PRINT Zanna.Option.UnwrapOrI64(l.FindOption(b), -1)
PRINT Zanna.Option.UnwrapOrI64(l.FindOption(c), -1)

' Insert in the middle: [a, b, c]
l.Insert(1, b)

PRINT Zanna.Option.UnwrapOrI64(l.FindOption(a), -1)
PRINT Zanna.Option.UnwrapOrI64(l.FindOption(b), -1)
PRINT Zanna.Option.UnwrapOrI64(l.FindOption(c), -1)

' Append via Insert(index == Count): [a, b, c, d]
l.Insert(l.Count, d)
PRINT Zanna.Option.UnwrapOrI64(l.FindOption(d), -1)

' Remove missing element
IF l.Remove(e) THEN
  PRINT 1
ELSE
  PRINT 0
END IF

' Add duplicate and remove only first occurrence: [a, b, c, d, a] -> remove(a) -> [b, c, d, a]
l.Push(a)
PRINT l.Count

IF l.Remove(a) THEN
  PRINT 1
ELSE
  PRINT 0
END IF

PRINT l.Count
PRINT Zanna.Option.UnwrapOrI64(l.FindOption(a), -1)

END
