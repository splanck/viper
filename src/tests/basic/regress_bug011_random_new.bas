' BUG-011 regression: NEW should work for Viper.Math.Random
DIM rng AS OBJECT = NEW Viper.Math.Random(42)
DIM val1 AS INTEGER = Viper.Math.Random.NextInt(1000)
PRINT val1 > -1
PRINT "ok"
