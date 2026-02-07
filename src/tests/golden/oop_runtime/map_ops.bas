DIM m AS Viper.Collections.Map
m = NEW Viper.Collections.Map()

DIM a AS Viper.Collections.List
a = NEW Viper.Collections.List()
DIM dflt AS Viper.Collections.List
dflt = NEW Viper.Collections.List()
DIM v1 AS Viper.Collections.List
v1 = NEW Viper.Collections.List()
DIM v2 AS Viper.Collections.List
v2 = NEW Viper.Collections.List()

m.Set("a", a)

IF Viper.Core.Object.RefEquals(m.GetOr("a", dflt), a) THEN
  PRINT 1
ELSE
  PRINT 0
END IF

IF Viper.Core.Object.RefEquals(m.GetOr("missing", dflt), dflt) THEN
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

IF Viper.Core.Object.RefEquals(m.GetOr("k", dflt), v1) THEN
  PRINT 1
ELSE
  PRINT 0
END IF

IF m.SetIfMissing("k", v2) THEN
  PRINT 1
ELSE
  PRINT 0
END IF

IF Viper.Core.Object.RefEquals(m.GetOr("k", dflt), v1) THEN
  PRINT 1
ELSE
  PRINT 0
END IF

END
