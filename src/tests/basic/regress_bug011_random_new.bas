' BUG-011 regression: NEW should work for Zanna.Math.Random
DIM rng AS OBJECT = NEW Zanna.Math.Random(42)
DIM val1 AS INTEGER = Zanna.Math.Random.NextInt(1000)
PRINT val1 > -1
PRINT "ok"
