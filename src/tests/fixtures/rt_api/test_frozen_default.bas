' test_frozen_default.bas — FrozenSet, FrozenMap, DefaultMap
DIM dm AS OBJECT
DIM fs AS OBJECT
DIM fm AS OBJECT
DIM keys AS OBJECT
DIM count AS INTEGER

dm = Zanna.Collections.DefaultMap.New(Zanna.Core.Box.Str("N/A"))
dm.Set("name", "zanna")
dm.Set("lang", "zia")
keys = dm.Keys()
count = keys.Count
PRINT count

fs = Zanna.Collections.FrozenSet.Empty()
PRINT fs.Count

fm = Zanna.Collections.FrozenMap.Empty()
PRINT fm.Count

PRINT "done"
END
