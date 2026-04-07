' test_frozen_default.bas — FrozenSet, FrozenMap, DefaultMap
DIM dm AS OBJECT
DIM fs AS OBJECT
DIM fm AS OBJECT
DIM keys AS OBJECT
DIM count AS INTEGER

dm = Viper.Collections.DefaultMap.New(Viper.Core.Box.Str("N/A"))
dm.Set("name", "viper")
dm.Set("lang", "zia")
keys = dm.Keys()
count = keys.Length
PRINT count

fs = Viper.Collections.FrozenSet.Empty()
PRINT fs.Length

fm = Viper.Collections.FrozenMap.Empty()
PRINT fm.Length

PRINT "done"
END
