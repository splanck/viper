' test_random_vec.bas — Viper.Math.Random + Vec2 + Vec3 + Easing + PerlinNoise
DIM rng AS OBJECT
LET rng = NEW Viper.Math.Random(42)
PRINT rng.Next()
PRINT rng.NextInt(1, 10)
PRINT rng.NextDouble()

DIM rng2 AS OBJECT
LET rng2 = NEW Viper.Math.Random(42)
PRINT rng2.Next()

' Vec2
DIM v AS OBJECT
LET v = NEW Viper.Math.Vec2(3.0, 4.0)
PRINT v.X
PRINT v.Y
PRINT v.Length()
PRINT v.Dot(v)
DIM vn AS OBJECT
LET vn = v.Normalize()
PRINT vn.Length()
DIM va AS OBJECT
LET va = v.Add(NEW Viper.Math.Vec2(1.0, 1.0))
PRINT va.X
DIM vs AS OBJECT
LET vs = v.Scale(2.0)
PRINT vs.X
PRINT Viper.Math.Vec2.Distance(NEW Viper.Math.Vec2(0.0,0.0), NEW Viper.Math.Vec2(3.0,4.0))

' Vec3
DIM v3 AS OBJECT
LET v3 = NEW Viper.Math.Vec3(1.0, 2.0, 3.0)
PRINT v3.X
PRINT v3.Y
PRINT v3.Z
PRINT v3.Length()
DIM v3n AS OBJECT
LET v3n = v3.Normalize()
DIM v3c AS OBJECT
LET v3c = v3.Cross(NEW Viper.Math.Vec3(0.0, 0.0, 1.0))
PRINT v3c.X

' Easing
PRINT Viper.Math.Easing.Linear(0.5)
PRINT Viper.Math.Easing.EaseInQuad(0.5)
PRINT Viper.Math.Easing.EaseOutQuad(0.5)
PRINT Viper.Math.Easing.EaseInOutQuad(0.5)

' PerlinNoise
DIM pn AS OBJECT
LET pn = NEW Viper.Math.PerlinNoise(42)
DIM noise AS DOUBLE
LET noise = pn.Noise2D(1.0, 1.0)
PRINT noise > -1.0
PRINT noise < 1.0

PRINT "done"
END
