DIM l AS Viper.Collections.List
l = NEW Viper.Collections.List()

DIM a AS Viper.Collections.List
DIM b AS Viper.Collections.List
DIM c AS Viper.Collections.List
DIM d AS Viper.Collections.List
DIM e AS Viper.Collections.List

a = NEW Viper.Collections.List()
b = NEW Viper.Collections.List()
c = NEW Viper.Collections.List()
d = NEW Viper.Collections.List()
e = NEW Viper.Collections.List()

' Has on empty list
IF l.Has(a) THEN
  PRINT 1
ELSE
  PRINT 0
END IF

' Seed list: [a, c]
l.Push(a)
l.Push(c)

PRINT Viper.Option.UnwrapOrI64(l.FindOption(a), -1)
PRINT Viper.Option.UnwrapOrI64(l.FindOption(b), -1)
PRINT Viper.Option.UnwrapOrI64(l.FindOption(c), -1)

' Insert in the middle: [a, b, c]
l.Insert(1, b)

PRINT Viper.Option.UnwrapOrI64(l.FindOption(a), -1)
PRINT Viper.Option.UnwrapOrI64(l.FindOption(b), -1)
PRINT Viper.Option.UnwrapOrI64(l.FindOption(c), -1)

' Append via Insert(index == Count): [a, b, c, d]
l.Insert(l.Count, d)
PRINT Viper.Option.UnwrapOrI64(l.FindOption(d), -1)

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
PRINT Viper.Option.UnwrapOrI64(l.FindOption(a), -1)

END
