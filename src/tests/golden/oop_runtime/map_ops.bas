DIM m AS Zanna.Collections.Map
m = NEW Zanna.Collections.Map()

DIM a AS Zanna.Collections.List
a = NEW Zanna.Collections.List()
DIM dflt AS Zanna.Collections.List
dflt = NEW Zanna.Collections.List()
DIM v1 AS Zanna.Collections.List
v1 = NEW Zanna.Collections.List()
DIM v2 AS Zanna.Collections.List
v2 = NEW Zanna.Collections.List()

m.Set("a", a)

IF Zanna.Core.Object.RefEquals(m.GetOr("a", dflt), a) THEN
  PRINT 1
ELSE
  PRINT 0
END IF

IF Zanna.Core.Object.RefEquals(m.GetOr("missing", dflt), dflt) THEN
  PRINT 1
ELSE
  PRINT 0
END IF

IF m.Has("missing") THEN
  PRINT 1
ELSE
  PRINT 0
END IF

IF m.SetIfMissing("k", v1) THEN
  PRINT 1
ELSE
  PRINT 0
END IF

IF Zanna.Core.Object.RefEquals(m.GetOr("k", dflt), v1) THEN
  PRINT 1
ELSE
  PRINT 0
END IF

IF m.SetIfMissing("k", v2) THEN
  PRINT 1
ELSE
  PRINT 0
END IF

IF Zanna.Core.Object.RefEquals(m.GetOr("k", dflt), v1) THEN
  PRINT 1
ELSE
  PRINT 0
END IF

END
